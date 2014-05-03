#include "rss2.h"

/* rss2.c 
 * RSS Reader.
 * Author: Anton L.
 * 
 * Refactor: Robin D.
 * Fixed bugs; 
 *	Not fetching whole RSS document, 
 *  Cruft at titles, 
 *	Segmentation fault in extract_content, (was not error checking strstr)
 *  Fixed incorrect memory allocations.
 *
 * Additions/Modifications
 *  RSS Fething now threaded.
 *  Content extractor no longer public.
 *  Variable RSS sources.
 *  Source formatted as GNU, changed variable/method names.
 *  ReAdded item count.
 *  Rewrote socket code for clarity and error tolerance.
 */

pthread_t rss_thread;

// fatal: object CAN be NULL here. [fixed] ~RD
char* extract_content(char* object, char* open, char* close)
{
	char* begin  = strstr(object, open);	//both methods are safe to call with NULL. ~RD
	char* end    = strstr(object, close);

	if (begin == NULL || end == NULL)	//if no start or endpoint is found, object/open/close is invalid. Segfault no more. ~RD
		return "";

	int i, length = strlen(begin) - strlen(end) - strlen(open);
	char* data = malloc(length + 1);	//warning: last byte unused. Added nullchar. ~RD

	for(i = 0; i < length; i++)
		data[i] = begin[strlen(open) + i];

	data[length] = '\0';				//nullchar ~RD
	return data;
}

static void* get_feed(void* arg)
{
	item_t item;					// Removed unused variables ~RD
	items_t* content = (items_t*) arg;
	int sockfd, i, counter = 0;
	char* request, *buffer, *tmp;
	struct addrinfo hints, *res;

	request = malloc(sizeof(uri) + sizeof(host) + 31 * sizeof(char));
	sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", uri, host);


	//rewrote socket code for clarity. ~RD
	if((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) 
		return;

	memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(host, PORT, &hints, &res);

    //nested sock events for error tolerance. ~RD
    if ((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) > -1)
    {
        if (connect(sockfd, res->ai_addr, res->ai_addrlen) > -1)
        {
            send(sockfd, request, strlen(request), 0);	//non blocking read.  ~RD
			usleep(TIMEOUT);							//timeout 1.5s buffer fill, single read. ~RD
			buffer = malloc(BYTEBUFFER);				//27.rss equals 35.5K bytes at 20 items,  ~RD
			recv(sockfd, buffer, BYTEBUFFER, 0);
        } 
        else
        	return;
    }
    else
    	return;

	//fatal: if only a partial response is received tags will break, getBetweenTags will return a null pointer. 
	//See main for example. [fixed]  ~RD
	counter = 0;

	while(strstr(buffer, "<item>") != NULL && counter < MAX_ITEMS)
	{
		if(counter == 0) 
			content->item = (item_t*) malloc(sizeof(item_t)); 
		else												
			content->item = (item_t*) realloc(content->item, sizeof(item_t) * (counter + 1));// stack fragmentation -> inefficient. ~RD

		tmp = extract_content(buffer, "<item>", "</item>");				// +1?, malloc for already allocated string? [Fixed by remove] ~RD
		tmp = extract_content(buffer, "<item>", "</item>");
		item.title = extract_content(tmp, "<title>", "</title>");		//title contained cruft, fixed in extract_content ~RD
		item.link  = extract_content(tmp, "<link>", "</link>");
		item.description = extract_content(tmp, "<description>", "</description>");
		content->item[counter] = item; 									//readded: update item counter. ~RD
		free(tmp);

		strcpy(buffer, strstr(buffer, "</item>"));	//~RD
		buffer[0] = 128;					//safer character (unset ascii), 0 leading tags would crash the RSS. ~RD
		counter++;
	}
	content->item_count = counter;
	free(buffer);
	free(request);	//free request too. ~RD
	close(sockfd);
	return;
}

//public function, is threaded. call with: items_t xxx; rss_feed(&test);
void rss_feed(items_t* items)
{
	pthread_create(&rss_thread, NULL, get_feed, items);
}

//main is kept for testing. ~RD
int main()
{
	int i;
	items_t items;
	rss_feed(&items);  

	//is threaded.. :o
	for (i = 0; i < 50; i++)
	{
		printf("!"); 
		fflush(stdout);
		usleep((int) TIMEOUT / 50 + 5000);
	}

	for(i = 0; i < items.item_count; i++)
		puts(items.item[i].title);		// print all titles found in feed

	// Bug example. Cause: An incomplete/malicious response from the server. [Now Fixed] ~RD
	printf("segfault by intent... -> "); fflush(stdout);
	printf("%s", extract_content("<item> the item of destruction </it", "<item>", "</item>"));

	return 1;
}