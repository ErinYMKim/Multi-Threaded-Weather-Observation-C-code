#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <curl/curl.h>
#include "cJSON.h"

#define MAX_STATIONS 100
#define MAX_NAME_LENGTH 50
#define MAX_ID_LENGTH 20
#define BUFFER_SIZE 10240

typedef struct MemoryStruct {
    char *memory;
    size_t size;
} MemoryStruct;

typedef struct Station {
    char name[MAX_NAME_LENGTH];
    char id[MAX_ID_LENGTH];
} Station;

// Definition for storing weather data
typedef struct WeatherStation {
    char name[MAX_NAME_LENGTH];
    char id[MAX_ID_LENGTH];
    double temperature;
    int humidity;
    char rain[MAX_NAME_LENGTH];
    struct WeatherStation *next;
} WeatherStation;

WeatherStation *head = NULL;
//int newStationAdded = 0;

pthread_mutex_t lock;
pthread_cond_t cond;

int readStationData(const char*, Station*, int*);
void printStationIDs(Station*, int);
char* findStationIDByName(Station*, int, const char*);
size_t writeCallback(void*, size_t, size_t, void*);
char* sendHTTPRequest(const char*);
void UpdateWeatherInfo(WeatherStation**);
int addNewStationToList(const char*, const char*);
void removeStation(const char*);
void printStations(void);
void* weatherUpdateTask();
void* userInterfaceTask(void*);

int readStationData(const char* filename, Station* stations, int* numStations)
{
    FILE *file = fopen(filename, "r");
    if(!file)
    {
        fprintf(stderr, "Failed to open file %s\n", filename);
        return 1;
    }

    char line[100];
    *numStations = 0;
    while((*numStations < MAX_STATIONS) && fgets(line, sizeof(line), file))
    {
        if(sscanf(line, "%s %s", stations[*numStations].id, stations[*numStations].name) == 2)
            (*numStations)++;
    }
    fclose(file);
    return 0;
}

void printStationIDs(Station* stations, int numStations)
{
    printf("Available stations:\n");
    for(int i = 0; i < numStations; ++i)
    {
        printf("%s\n", stations[i].name);
    }
}

char* findStationIDByName(Station* stations, int numStations, const char* name)
{
    for(int i = 0; i < numStations; ++i)
    {
        if(strcasecmp(stations[i].name, name) == 0)
        {
            return stations[i].id;       
        }
    }
    return NULL;
}

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    size_t realSize = size* nmemb;
    MemoryStruct* mem = (MemoryStruct*)userp;

    char* ptr = realloc(mem->memory, mem->size + realSize +1);
    if(ptr == NULL)
    {
        fprintf(stderr, "not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realSize);
    mem->size += realSize;
    mem->memory[mem->size] = 0;

    return realSize;
}

char* sendHTTPRequest(const char* url)
{
    CURL *curl;
    CURLcode res;
    MemoryStruct chunk;
    chunk.memory = malloc(1); // will be grown as needed by realloc in the write callback
    chunk.size = 0; // no data at this point

    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/97.0.4692.71 Safari/537.36");
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return NULL;
        }

        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if(response_code != 200)
        {
            fprintf(stderr, "curl_easy_getinfo() failed: %ld\n", response_code);
            free(chunk.memory);
            curl_easy_cleanup(curl);
            return NULL;
        }
        curl_easy_cleanup(curl);
    }
    else
    {
        fprintf(stderr, "Failed to initialize libcurl\n");
        free(chunk.memory);
        return NULL;
    }
    return chunk.memory; // Caller must free this memory
}

void updateWeatherInfo(WeatherStation** station)
{
    char url[256];
    char* stationID = (*station)->id;
    snprintf(url, sizeof(url), "http://www.bom.gov.au/fwo/IDN60801/IDN60801.%s.json", stationID);

    // Use the sendHTTPRequest function to fetch the weather data for the station
    char* response = sendHTTPRequest(url);
    if(!response)
    {
        fprintf(stderr, "Failed to send HTTP request or receive response\n");
        return;
    }

    // Parse the JSON response
    cJSON* json = cJSON_Parse(response);
    if(!json)
    {
        fprintf(stderr, "Failed to parse JSON response\n");
        free(response);
        return;
    }

    // Navigate to the required data in the JSON structure
    cJSON* observations = cJSON_GetObjectItemCaseSensitive(json, "observations");
    if(!observations)
    {
        fprintf(stderr, "Observations not found\n");
        cJSON_Delete(json);
        free(response);
        return;
    }
    
    cJSON *dataArray = cJSON_GetObjectItemCaseSensitive(observations, "data");
    if(!dataArray) 
    {
        fprintf(stderr, "Data array not found\n");
        cJSON_Delete(json);
        free(response);
        return;
    }
 
    cJSON *firstItem = cJSON_GetArrayItem(dataArray, 0);
    if(!firstItem) 
    {
        fprintf(stderr, "First item not found\n");
        cJSON_Delete(json);
        free(response);
        return;
    }

    // Extract weather information
    cJSON *temp = cJSON_GetObjectItemCaseSensitive(firstItem, "air_temp");
    cJSON *humidity = cJSON_GetObjectItemCaseSensitive(firstItem, "rel_hum");
    cJSON *rain = cJSON_GetObjectItemCaseSensitive(firstItem, "rain_trace");
    
    // Update the station's weather information
    if(temp != NULL) (*station)->temperature = temp->valuedouble;
    if(humidity != NULL) (*station)->humidity = humidity->valueint;
    if(rain != NULL) strncpy((*station)->rain, cJSON_GetStringValue(rain), MAX_NAME_LENGTH);

    // Cleanup
    cJSON_Delete(json);
    free(response);
}

int addNewStationToList(const char* stationName, const char* stationID)
{
    for(WeatherStation *node = head; node != NULL; node = node->next)
    {
        if(strcasecmp(node->name, stationName) == 0)
        {
            printf("\n[%s] already added\n", stationName);
            return -1;
        }
    }
    // Allocate new node
    WeatherStation *newNode = malloc(sizeof(WeatherStation));
    if(!newNode) return -1; // Allocation failed
    strncpy(newNode->name, stationName, MAX_NAME_LENGTH);
    strncpy(newNode->id, stationID, MAX_ID_LENGTH);
    // Insert node at the beginning of the list for simplicity
    newNode->next = head;
    head = newNode;
    printf("%s added successfully to WeatherStation.\n", stationName);
    return 1;
}

void removeStation(const char* stationName)
{
    WeatherStation **nodePtr = &head;
    while(*nodePtr && strcasecmp((*nodePtr)->name, stationName) != 0)
    {
        nodePtr = &(*nodePtr)->next;
    }

    if(*nodePtr)
    {
        WeatherStation *temp = *nodePtr;
        *nodePtr = (*nodePtr)->next;
        printf("%s removed successfully to WeatherStation.\n", stationName);
        free(temp);
    }
    else
    {
        printf("\nStation %s not found\n", stationName);
    }
}

void printStations()
{
    for(WeatherStation* node = head; node; node = node->next)
    {
        if(node != NULL)
        {
            printf("Station: < %s >\n", node->name);           
            printf("\tTemperature: %.2f°C\n", node->temperature);
            printf("\tHumidity: %d%%\n", node->humidity);
            printf("\tRain since 9am: %s\n", node->rain);
        }
    }
}

void* weatherUpdateTask()
{
    int updateInterval = 5; // Update interval in seconds
    while(1)
    {
        if(pthread_mutex_lock(&lock) != 0)
        {
            perror("pthread_mutex_lock");
            continue;
        }
//        printf("weatherUpdateTask.... 1\n");
        for(WeatherStation* node = head; node != NULL; node = node->next)
        {
            updateWeatherInfo(&node);        
        }

        // Use timed wait for updates
        struct timespec ts;
        if(clock_gettime(CLOCK_REALTIME, &ts) == -1)
        {
            perror("clock_gettime");
            pthread_mutex_unlock(&lock);
            continue;
        }
        ts.tv_sec += updateInterval;

        // Wait for signal to update all added stations' weather immediately or timeout after updateInterval    
//        printf("weatherUpdateTask.... 2\n");
        int rc = pthread_cond_timedwait(&cond, &lock, &ts);
        if(rc == ETIMEDOUT)
        {
//           printStations();
//           printf("\npthread_cond_timedwait timeout. sec:%ld (interval:%d)\n", ts.tv_sec, updateInterval);
        }
        else if(rc == 0)
        {
            printf("\nWeatherUpdateTask received signal\n");
        }
        else
        {
            perror("pthread_cond_timedwait");
        }
        if(pthread_mutex_unlock(&lock) != 0)
        {
            perror("pthread_mutex_unlock");
        }
    }
    return NULL;
}

void* userInterfaceTask(void* arg)
{
    Station* stations = (Station*)arg;
    int numStations;

    if(readStationData("station_data.txt", stations, &numStations))
    {
        printf("Failed to read station data\n");
        exit(1);
    }

    int choice;
    char userInput[MAX_NAME_LENGTH];
    while(1)
    {
        printf("\nMenu:\n");
        printf("1. Show available stations\n");
        printf("2. Add [station]\n");
        printf("3. Remove [station]\n");
        printf("4. Print\n");
        printf("5. Quit\n");
        printf("Choose a number: ");
       
        if(scanf("%d", &choice) != 1)
        {
            // Clear input buffer
            while(getchar() != '\n');
            printf("invalid input. Please enter a number.\n");
            continue;
        }

        switch(choice)
        {
            case 1:
                printStationIDs(stations, numStations);
                break;
            case 2:
                printf("Enter station name to add: ");
                scanf("%s", userInput);
                char* stationID = findStationIDByName(stations, MAX_STATIONS, userInput);
                if(stationID)
                {
                    if(pthread_mutex_lock(&lock) != 0)
                    {
                        perror("pthread_mutex_lock");
                    }
                    if(addNewStationToList(userInput, stationID) > 0)
                    {
                        pthread_cond_signal(&cond);
                    }
                    if(pthread_mutex_unlock(&lock) != 0)
                    {
                        perror("pthread_mutex_unlock");
                    }
                }
                else 
                {
                    printf("\nStation not found\n");
                }
                break;
            case 3:
                printf("Enter station name to remove: ");
                scanf("%s", userInput);
                if(pthread_mutex_lock(&lock) != 0)
                {
                    perror("pthread_mutex_lock");
                }
                removeStation(userInput);
                if(pthread_mutex_unlock(&lock) != 0)
                {
                    perror("pthread_mutex_unlock");
                }

                break;
            case 4:
                if(pthread_mutex_lock(&lock) != 0)
                {
                    perror("pthread_mutex_lock");
                }
                printStations();
                if(pthread_mutex_unlock(&lock) != 0)
                {
                    perror("pthread_mutex_unlock");
                }
                break;
            case 5:
                printf("Exiting\n");
                return NULL;
            case 8:
                for(int i = 0; i < numStations; i++)
                {
                    if(pthread_mutex_lock(&lock) != 0)
                    {
                        perror("pthread_mutex_lock");
                    }
                    if(addNewStationToList((stations+i)->name, (stations+i)->id) > 0)
                    {
                        pthread_cond_signal(&cond);
                    }
                    if(pthread_mutex_unlock(&lock) != 0)
                    {
                        perror("pthread_mutex_unlock");
                    }     
                }
                break;
            case 9:
                 for(int i = 0; i < numStations; i++)
                {
                    if(pthread_mutex_lock(&lock) != 0)
                    {
                        perror("pthread_mutex_lock");
                    }
                    removeStation((stations+i)->name);
                    if(pthread_mutex_unlock(&lock) != 0)
                    {
                        perror("pthread_mutex_unlock");
                    }
                }
                break;              
            default:
                printf("\nInvalid number. Try again\n");
        }
    }
}

int main()
{
    pthread_t weatherUpdateThread, userInterfaceThread;
    Station stations[MAX_STATIONS];

    if(pthread_mutex_init(&lock, NULL) != 0)
    {
        perror("Failed to initialize mutex");
        return 1;
    }

    if(pthread_cond_init(&cond, NULL) != 0)
    {
        perror("Failed to initialize condition variable");
        pthread_mutex_destroy(&lock);
        return 1;
    }

    // Start the weather update and user interface threads
    if(pthread_create(&weatherUpdateThread, NULL, weatherUpdateTask, NULL) != 0)
    {
        perror("Failed to create weather update thread");
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
        return 1;
    }

    if(pthread_create(&userInterfaceThread, NULL, userInterfaceTask, (void*)stations) != 0)
    {
        perror("Failed to create user interface thread");
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
        return 1;
    }

    // Wait for the UI thread to finish before exiting
    if(pthread_join(userInterfaceThread, NULL) != 0)
    {
        perror("Failed to join user interface thread");
        pthread_mutex_destroy(&lock);
        pthread_cond_destroy(&cond);
        return 1;
    }

    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);

    return 0;
}