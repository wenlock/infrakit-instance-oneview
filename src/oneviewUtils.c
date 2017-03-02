
// oneviewUtils.c

/*
 *
 * instance-infrakit-oneview - A Library for interacting with HPE OneView
 *
 * Dan Finneran <finneran@hpe.com>
 *
 * (c) Copyright [2017] Hewlett Packard Enterprise Development LP;
 *
 * This software may be modified and distributed under the terms
 * of the Apache 2.0 license.  See the LICENSE file for details.
 */


#include "oneview.h"
#include "oneviewHttp.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


   /*
    *
    * Init of session structure
    *
    */

oneviewSession *initSession()
{
    oneviewSession *session = malloc(sizeof(oneviewSession));
    session->cookie = NULL;
    session->address = NULL;
    session->version = "0"; // default to a zero header
    
    session->debug = malloc(sizeof(oneviewDebug));
    session->debug->usedAddress = malloc(1024);
    session->debug->buffer = NULL;
    return session;
}

oneviewQuery *initQuery()
{
    oneviewQuery *query = malloc(sizeof(oneviewQuery));
    query->count = 0;
    query->start = 0;
    query->query = NULL;
    query->filter = NULL;
    return query;
}

int stringMatch(const char *string1, const char *string2)
{
    if ((string1) && (string2)) {
        while (*string1 == *string2) {
            // if both points in strings are null break out of loop
            if (*string1 == '\0' || *string2 == '\0') {
                break;
            }
            // move onto next character comparison
            string1++;
            string2++;
        }
        // Have we reached the end of the string whilst both have been the same value
        if (*string1 == '\0' && *string2 == '\0')
            return 1; // Return true (compiler)
        else
            return 0; // Return false
    }
    return 0;
}



char *findVersionInJSON (char *httpBuffer)
{
    // This is a function for handling the tiny piece of JSON that is returned from http://<appliance>/version
    // The json should always follow the same structure
    // {"currentVersion":XXX,"minimumVersion":X}
    // We're only after the currentVersion
    
    // Ensure that there was content returned
    if (httpBuffer) {
        // Check the length of the raw text that was returned
        size_t stringLength = strlen(httpBuffer);
        if (stringLength >= 41) {
            char *versionKey = strstr(httpBuffer, "currentVersion");
            if (versionKey) {
                versionKey = versionKey + 16; // Move Pointer to the end of the comma
                return strdup(strtok(versionKey, ",")); // remove characters from the right up to the comma
            }
        }
    }
    return NULL;
}

char *findCookieInJSON (char *httpBuffer)
{
    // This is a function for handling the tiny piece of JSON that is returned from http://<appliance>/login-sessions
    // The json should always follow the same structure
    // {"partnerData":"","sessionID":X}
    // We're only after the sessionID
    
    // Check one, ensure that the key is "sessionID"
    size_t stringLength = strlen(httpBuffer); // ideally should be 41 characters
    if (stringLength >= 41) {
        char *versionKey = strstr(httpBuffer, "sessionID");
        if (versionKey) {
            versionKey = versionKey + 12; // Move Pointer to the end of the comma
            versionKey[strlen(versionKey) -2] = '\0'; // remove the last two characters
        
            return strdup(strtok(versionKey, ",")); // remove characters from the right up to the comma
        }
    }
    return NULL;
}

char *createJSONLoginText(oneviewSession *session)
{
    // Check to make sure that eveything has been allocated succesfully otherwise we'll segfault
    if (session && session->username && session->password) {
        if (session->debug->buffer) { // if the buffer is being used, free that memory before re-allocating
            free (session->debug->buffer);
        }
        char buffer[1024]; // 1K buffer for creating the json string
        sprintf(buffer, "{\"userName\":\"%s\",\"password\":\"%s\"}", session->username, session->password);
        session->debug->buffer = malloc(strlen(buffer));
        memcpy(session->debug->buffer, buffer, strlen(buffer));
        return session->debug->buffer;
    }
    return NULL;
}

   /*
    *
    * Utility functions (version identification and logging in)
    *
    */

void setOVHeaders(oneviewSession *session)
{
    if (session)
    {
        appendHttpHeader("Content-Type: application/json");
        if (session->version > 0)
        {
            char versionHeader[1024]; // 1k buffer for version header
            sprintf(versionHeader, "X-API-version: %s", session->version); // Append the version to the header
            appendHttpHeader(versionHeader);
        }
        if (session->cookie)
        {
            createHeader("Auth: ", session->cookie);
        }
    }
}


char * identifyOneview(oneviewSession *session)
{
    // This function will iterate through the available systems and attempt to identify the system and it's version
    if (!session->address) {
        // No URL
        return 0;
    }
    //Begin Checks check
    
    char *httpData;

    // Wipe the contents of the allocated memory
    memset(session->debug->usedAddress, 0, sizeof(1024));
    // Create the url and store it in the debug structure
    createURL(session, "/rest/version");
    
    SetHttpMethod(DCHTTPGET);
    httpData = httpFunction(session->debug->usedAddress);
    if (httpData) {
        char *version = strdup(findVersionInJSON(httpData));
    
        // Tidy allocated memory

        free(httpData);
        return version;

    }
    return NULL;
}

int ovLogin(oneviewSession *session)
{
    if (session->version == NULL) {
        printf("[WARNING], ensure that version has been discovered before attempting to use API for logging in, undefined behaviour\n");
    }
    char *httpData;
    char *json_text = createJSONLoginText(session);
    // Wipe the contents of the allocated memory
    memset(session->debug->usedAddress, 0, 1024);
    
    // Create the url and store it in the debug structure
    createURL(session, "/rest/login-sessions");

    // Call to HP OneView API

    // Add the JSON test to be posted
    setHttpData(json_text);
    // pass the session struct for the X-API_Version
    setOVHeaders(session);
    // Sett HTTP Method to POST
    SetHttpMethod(DCHTTPPOST);
    // Call the function
    httpData = httpFunction(session->debug->usedAddress);
    
    if(!httpData) {
        free(json_text);
        return EXIT_FAILURE;
    }
    
    session->cookie = findCookieInJSON(httpData); // If there is incorrect credentials then null will be returned
    
    if (!session->cookie) {
        free (json_text);
        free(httpData);
        return EXIT_FAILURE;
    }
    
    if (getenv("OV_DEBUG"))
        printf("[INFO] JSON:\t %s\n", json_text);
    
    // release allocated memory
    free (json_text);
    free(httpData);
    return EXIT_SUCCESS;
}

int ovPostProfile(oneviewSession *session, char *profile)
{
    if (session->version == NULL) {
        printf("[WARNING], ensure that version has been discovered before attempting to use API for logging in, undefined behaviour\n");
    }
    char *httpData;

    // Wipe the contents of the allocated memory
    memset(session->debug->usedAddress, 0, 1024);
    
    // Create the url and store it in the debug structure
    createURL(session, "/rest/server-profiles");
    
    // Call to HP OneView API
    
    // Add the JSON test to be posted
    setHttpData(profile);
    // pass the session struct for the X-API_Version
    setOVHeaders(session);
    // Sett HTTP Method to POST
    SetHttpMethod(DCHTTPPOST);
    // Call the function
    httpData = httpFunction(session->debug->usedAddress);
    
    if(!httpData) {
        free(profile);
        return EXIT_FAILURE;
    }

    // release allocated memory
    free (profile);
    free (httpData);
    return EXIT_SUCCESS;
}

int ovDeleteProfile(oneviewSession *session, char *profile)
{
    if (session->version == NULL) {
        printf("[WARNING], ensure that version has been discovered before attempting to use API for logging in, undefined behaviour\n");
    }
    char *httpData;
    
    // Wipe the contents of the allocated memory
    memset(session->debug->usedAddress, 0, 1024);
    
    // Create the url and store it in the debug structure
    createURL(session, profile);
    
    // pass the session struct for the X-API_Version
    setOVHeaders(session);
    // Sett HTTP Method to POST
    SetHttpMethod(DCHTTPDELETE);
    // Call the function
    httpData = httpFunction(session->debug->usedAddress);
    
    if(!httpData) {
        free(profile);
        return EXIT_FAILURE;
    }
    
    // release allocated memory
    free (profile);
    free (httpData);
    return EXIT_SUCCESS;
}



 /*
  *
  * File Operations
  *
  */

int writeDataToUserFile(oneviewSession *session)
{
    if (!session->cookie) {
        printf("Not logged into HP OneView host\n");
        return 0; // No Cookie to write
    }
    FILE *fp;
    char filepath[1024]; //large file path
    
    sprintf(filepath, "%s/.%s_ov", getenv("HOME"), session->address);
    
    fp = fopen(filepath, "w");
    if (fp) { /*file opened succesfully */
        fputs(session->cookie, fp);
        fclose(fp);
    } else {
        printf("Error opening file %s \n", filepath);
        return 0;
    }
    return 1; // success
}

int readDataFromUserFile(oneviewSession *session)
{
    FILE *fp;
    char buffer[1024]; //size of Session ID
    char filepath[1000]; //large file path
    
    sprintf(filepath, "%s/.%s_ov", getenv("HOME"), session->address);
    
    fp = fopen(filepath, "r");
    if (fp) { /*file opened succesfully */
        fgets(buffer, sizeof(buffer), fp);
        fclose(fp);
    } else {
        printf("Error opening file %s \n", filepath);
        return 0; // failed to open file
    }
    session->cookie = strdup(buffer);
    return 1; // success, duplicated string from stack to heap
}
