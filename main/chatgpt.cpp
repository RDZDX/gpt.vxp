#include "Console.h"
#include "chatgpt.h"
#include "Console_io.h"
#include "thread.h"
#include "CircleBuf.h"
#include "cJSON.h"
#include <string.h>

// Free ChatGPT endpoint. You can use any endpoint you want
//const char* host = "inference.memgpt.ai";
const char* host = "api.openai.com";

// API path
//const char* path = "/chat/completions";
const char* path = "/v1/chat/completions";

extern CircleBuf circlebuf;

void https_request(const char* host, const char* path, const char* body, char* rbody, int &rbody_size);

const char* messages_template[2][2] = 
{
	{"system", "You are a helpful assistant."},
	{"user", "{promt}"}
};

#define JSON_PUT(j, n, s) if (cJSON_AddStringToObject((j), (n), (s)) == NULL) goto error;

struct json_and_promt{
	json_and_promt(cJSON* b, cJSON* p){ base = b, promt = p; }
	cJSON* base;
	cJSON* promt;
};

json_and_promt generate_base_request_gpt_json(){
	cJSON* base = cJSON_CreateObject();
	if(base == NULL) goto error;

//	JSON_PUT(base, "model", "memgpt-openai");
        JSON_PUT(base, "model", "gpt-5.6-luna");
	JSON_PUT(base, "user", "00000000-0000-0000-0000-18937ffe6b0f");
	
	{
		cJSON* promt = 0;
		cJSON* messages = cJSON_AddArrayToObject(base, "messages");
		if (messages == NULL) goto error;

		for (int i = 0; i < 2; ++i) {
			cJSON* message = cJSON_CreateObject();

			JSON_PUT(message, "role", messages_template[i][0]);
			if ((promt = cJSON_AddStringToObject(message, "content", messages_template[i][1])) == NULL) goto error;

			cJSON_AddItemToArray(messages, message);
		}

		return json_and_promt(base, promt);
	}

	error:
	cprintf("Generate JSON failed\n");
    cJSON_Delete(base);
	return json_and_promt(0, 0);
}

char* get_gpt_answer(char* json){
	static char gpt_answer[1024*10];
	gpt_answer[0] = '\0';

	cJSON* answer = cJSON_Parse(json);

	if (answer == NULL){
		const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL)
        {
            cprintf("JSON Error before: %s\n", error_ptr);
        }
	}else{


cJSON *choice = NULL;

cJSON* choices =
    cJSON_GetObjectItemCaseSensitive(answer, "choices");

if (!cJSON_IsArray(choices)) {

    cJSON* error =
        cJSON_GetObjectItemCaseSensitive(answer, "error");

    if (cJSON_IsObject(error)) {

        cJSON* message =
            cJSON_GetObjectItemCaseSensitive(error, "message");

        if (cJSON_IsString(message)) {
            cprintf("OpenAI error: ");
            cprintf("%s", message->valuestring);
            cprintf("\n");
        }
    }

    cJSON_Delete(answer);
    return gpt_answer;
}

cJSON_ArrayForEach(choice, choices)
{
    cJSON* message =
        cJSON_GetObjectItemCaseSensitive(choice, "message");

    if (cJSON_IsObject(message)) {

        cJSON* content =
            cJSON_GetObjectItemCaseSensitive(message, "content");

        if (cJSON_IsString(content) &&
            content->valuestring != NULL)
        {
            strncpy(
                gpt_answer,
                content->valuestring,
                sizeof(gpt_answer) - 1
            );

            gpt_answer[sizeof(gpt_answer) - 1] = '\0';

            break;
        }
    }
}


	}

    cJSON_Delete(answer);

	return gpt_answer;
}

char* get_promt(){
    static char promtbuf[1024*4];
    int promtbuf_size = 0;

    while(1){
        char c = circlebuf.pop_blok();

        if(c == '\177'){
            if (promtbuf_size > 0) {
                console_str_in("\b \b");
                promtbuf_size--;
            }
        }else if(c == '\r' && circlebuf.view_blok() == '\n'){
            circlebuf.pop();
            promtbuf[promtbuf_size] = '\0';
            return promtbuf;
        }else{
            if (promtbuf_size < (int)sizeof(promtbuf) - 1) {
                promtbuf[promtbuf_size++] = c;
            }
        }
    }
}

char answer_buf[1024*10];

void main_gpt(){
	json_and_promt request = generate_base_request_gpt_json();
	if(request.base == 0)
		thread_end();
		
	cprintf("ChatGPT client for Nokia\n");
	cprintf("By gvl610 & Ximik_Boda\n");

	while(1){
		cprintf("Client: ");

		char *promt = get_promt();

		cprintf("\n\n");
		
		cJSON_SetValuestring(request.promt, promt); //update promt in json
		
		char *body = cJSON_PrintUnformatted(request.base); //json to string

		int res_size = 1024*10;
		https_request(host, path, body, answer_buf, res_size); //http request
//		answer_buf[res_size] = '0';
		if (res_size >= sizeof(answer_buf))
		    res_size = sizeof(answer_buf) - 1;

		answer_buf[res_size] = '\0';


		char *gpt_answer = get_gpt_answer(answer_buf); //parse json

		vm_free(body);

		cprintf("ChatGPT: ");
		console_str_in(gpt_answer);
		cprintf("\n\n");
	}

	cJSON_Delete(request.base);

	thread_end(); //return is forbiddenn
}
