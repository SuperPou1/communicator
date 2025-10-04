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

#define BUFFER_SIZE 512
#define MAX_LOGIN_LEN 50
#define MAX_PASS_LEN 50

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

int main(int argc, char *argv[]) {
	int islogin = 0;
	char server[512];
	char confpath[512];
	struct json_object *commandjson = json_object_new_object();;
	struct json_object *responsejson;
	struct json_object *confjson;

	strcpy(confpath, getenv("HOME"));
	strcat(confpath,"/.config/communicator/");

	// initialize curl
	curl_global_init(CURL_GLOBAL_ALL);
	CURL *handle = curl_easy_init();

	mkdir_p(confpath);
	strcat(confpath,"login.json"); // append the file to the path after creating the directory

	FILE *file = fopen(confpath, "r+");
	if (file && argc < 2) { printf("Please specify an action\n"); return 1; }
	if (!file || strstr("login", argv[1])) {
		file = fopen(confpath, "wb");
		if (!file) { perror("Something went wrong creating config"); return 1;};
		char confstr[512];
		char login[MAX_LOGIN_LEN];
		char pass[MAX_PASS_LEN];

		islogin = 1; // whether we are logging in, relevant for writing the config after the request

		strcpy(server, "http://100.125.92.92:8000"); // default server
		printf("Please log in\n");
		printf("Login: ");
		fgets(login, MAX_LOGIN_LEN, stdin);
		printf("Password: ");
		fgets(pass, MAX_PASS_LEN, stdin);
		login[strcspn(login, "\n")] = 0;
		pass[strcspn(pass, "\n")] = 0;

		json_object_object_add(commandjson, "Username", json_object_new_string(login));
		json_object_object_add(commandjson, "Password", json_object_new_string(pass));
		json_object_object_add(commandjson, "Command", json_object_new_string("login"));

	} else {
		// read config from file
		fseek(file, 0, SEEK_END);
		long filesize = ftell(file);
		rewind(file);
		char *confstr = malloc(filesize + 1);

		size_t bytes_read = fread(confstr, 1, filesize, file);
		confstr[bytes_read] = '\0';

		confjson = json_tokener_parse(confstr);

		strcpy(server, json_object_get_string(json_object_object_get(confjson, "Server")));
		json_object_object_add(commandjson, "Token", json_object_object_get(confjson, "Token"));

		json_object_object_add(commandjson, "Command", json_object_new_string(argv[1])); // use first arg as command
	}


	struct Buffer response = {{0}, 0};

	curl_easy_setopt(handle, CURLOPT_URL, server);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_to_buffer);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response.data);

	printf("Request JSON: %s\n", json_object_to_json_string(commandjson)); // debug
	curl_easy_setopt(handle, CURLOPT_POSTFIELDS, json_object_to_json_string(commandjson));
	curl_easy_perform(handle);

	responsejson = json_tokener_parse(response.data);
	json_object_object_add(responsejson, "Server", json_object_new_string(server)); // add the server to the returned request to write it to config
	printf("Welcome to communicator!\n%s\n", json_object_to_json_string_ext(responsejson, JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY)); // debug
	const char *responsestr = json_object_to_json_string(responsejson);
	if (islogin)
		fwrite(responsestr, 1, strlen(responsestr), file);
	fclose(file);
}
