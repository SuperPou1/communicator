#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>

size_t write_data(void *buffer, size_t size, size_t nmemb, void *userp);

int main(int argc, char *argv[]) {
	curl_global_init(CURL_GLOBAL_ALL);
	int file = open("./test.txt", O_WRONLY | O_CREAT | O_TRUNC);
	CURL *handle = curl_easy_init();
	curl_easy_setopt(handle, CURLOPT_URL, "http://100.125.92.92:8000/");
	curl_easy_setopt(handle, CURLOPT_READDATA, file);
	curl_easy_perform(handle);
	printf("Welcome to communicator!\n");
}
