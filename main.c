#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <getopt.h>

#define BUFFER_SIZE 512
#define MAX_LOGIN_LEN 50
#define MAX_PASS_LEN 50
#define MSG_SIZE 4096

#define HELP_MSG \
"Usage: communicator [options]\n" \
"Options:\n" \
"  -s, --server <address>   Specify the server address\n" \
"  -c, --command <cmd>      Send a command to the server\n" \
"  -l, --login              Perform a login action\n" \
"  -m, --message            Send a message\n" \


static struct option long_options[] = {
	{"server", required_argument, NULL, 's'},
	{"command", required_argument, NULL, 'c'},
	{"login", no_argument, NULL, 'l'},
	{"message", no_argument, NULL, 'm'},
	{NULL, 0, NULL, 0}
};

int mkdir_p(const char *path) {
	char tmp[1024];
	char *p = NULL;
	size_t len;

	snprintf(tmp, sizeof(tmp), "%s", path);
	len = strlen(tmp);
	if (tmp[len - 1] == '/')
		tmp[len - 1] = '\0';

	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	return mkdir(tmp, 0755);  // last dir
}

struct Buffer {
	char data[BUFFER_SIZE];
	size_t size;
};

size_t write_to_buffer(void *buffer, size_t size, size_t nmemb, void *userp) {
	struct Buffer *buf = (struct Buffer *)userp;
	size_t total = size * nmemb;

	if (buf->size + total >= BUFFER_SIZE) {
		total = BUFFER_SIZE - buf->size - 1; // leave space for null terminator
	}

	memcpy(buf->data + buf->size, buffer, total);
	buf->size += total;
	buf->data[buf->size] = '\0'; // null terminate

	return total;
}

int sendrq(struct json_object *requestjson, char *server, struct json_object **responsejson) {
	struct Buffer responsebuff = {{0}, 0};
	enum json_tokener_error responsestate;

	CURL *handle = curl_easy_init();

	curl_easy_setopt(handle, CURLOPT_URL, server);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_to_buffer);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &responsebuff.data);

	printf("Request JSON: %s\n", json_object_to_json_string(requestjson)); // debug
	curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json_object_to_json_string(requestjson));
	curl_easy_perform(handle);
	*responsejson = json_tokener_parse_verbose(responsebuff.data, &responsestate);
	if (responsestate > json_tokener_continue) { // anything higher than json_tokener_continue is an error
		return 1;
	}
	json_object_object_add(*responsejson, "Server", json_object_new_string(server)); // add the server to the returned response for debug and to write it to config
	printf("Response: %s\n",json_object_to_json_string(*responsejson));

	return 0;
}

int ac_account(int ac, char *server, char **token) {
	struct json_object *requestjson = json_object_new_object();
	struct json_object *responsejson;
	struct json_object *confjson;
	enum json_tokener_error confstate;
	char confpath[512];

	/* ac 0 = load config
	 * ac 1 = log in
	 * ac 2 = register
	 * ac 3 = log out
	 */

	strcpy(confpath, getenv("HOME"));
	strcat(confpath,"/.config/communicator/");

	mkdir_p(confpath);
	strcat(confpath,"login.json"); // append the file to the path after creating the directory
	FILE *file = fopen(confpath, "r+");
	if (ac == 0 || ac >= 3) {
		if (!file) {printf("Please log in first.\n"); return 1;}
		// read config from file
		fseek(file, 0, SEEK_END);
		long filesize = ftell(file);
		rewind(file);
		char *confstr = malloc(filesize + 1);

		size_t bytes_read = fread(confstr, 1, filesize, file);
		confstr[bytes_read] = '\0';

		confjson = json_tokener_parse_verbose(confstr, &confstate);
		if (confstate > json_tokener_continue) {
			printf("Error reading config, please try to log in again.\n");
			return 1;
		}
		strcpy(*token, json_object_get_string(json_object_object_get(confjson, "Token")));
		strcpy(server, json_object_get_string(json_object_object_get(confjson, "Server")));
		return 0;
	} else {
		file = fopen(confpath, "wb");
		if (!file) { perror("Something went wrong creating config\n"); return 1;};
	}

	char login[MAX_LOGIN_LEN];
	char pass[MAX_PASS_LEN];

	printf("Login: ");
	fgets(login, MAX_LOGIN_LEN, stdin);
	printf("Password: ");
	fgets(pass, MAX_PASS_LEN, stdin);
	login[strcspn(login, "\n")] = 0;
	pass[strcspn(pass, "\n")] = 0;

	json_object_object_add(requestjson, "Username", json_object_new_string(login));
	json_object_object_add(requestjson, "Password", json_object_new_string(pass));
	json_object_object_add(requestjson, "Command", json_object_new_string("login"));
	if (sendrq(requestjson, server, &responsejson)) {
		printf("Something went wrong while logging in, please check connection\n");
	}
	const char *responsestr = json_object_to_json_string(responsejson);
	fwrite(responsestr, 1, strlen(responsestr), file);
	fclose(file);
	return 0;
}

int main(int argc, char *argv[]) {
	int ch;
	int action = 0;
	char *server = malloc(512);
	char *token = malloc(33);
	struct json_object *requestjson = json_object_new_object();
	struct json_object *responsejson;
	// get options
	while ((ch = getopt_long(argc, argv, "s:c:lm", long_options, NULL)) != -1) {
		switch (ch) {
			case 'l':
				action = 1;
				break;
			case 'c':
				action = 4;
				json_object_object_add(requestjson, "Command", json_object_new_string(optarg));
				break;
			case 's':
				strcpy(server, optarg);
				break;
			case 'm':
				action = 5;
				break;
			default:
				fprintf(stderr, "Unknown option\n");
				return 1;
		}
	}

	// initialize curl
	curl_global_init(CURL_GLOBAL_ALL);

	if(action > 3) {
		ac_account(0, server, &token);
		json_object_object_add(requestjson, "Token", json_object_new_string(token));
	} else if (action > 0) {
		ac_account(action, server, &token);
		return 0;
	} else {
		printf("%s\n", HELP_MSG);
		return 1;
	}

	switch(action) {
		case 5:
			char msg[MSG_SIZE];
			char recipient[MAX_LOGIN_LEN];
			printf("Recipient: ");
			fgets(recipient, MAX_LOGIN_LEN, stdin);
			printf("Message: ");
			fgets(msg, MSG_SIZE, stdin);
			recipient[strcspn(recipient, "\n")] = 0;
			msg[strcspn(msg, "\n")] = 0;
			json_object_object_add(requestjson, "Command", json_object_new_string("send"));
			json_object_object_add(requestjson, "Message", json_object_new_string(msg));
			json_object_object_add(requestjson, "MessageUser", json_object_new_string(recipient));
			break;
	}

	if (sendrq(requestjson, server, &responsejson)) {
		printf("Server: %s\n", server);
		printf("Something went wrong sending request, please check connection\n");
		return 1;
	}
}
