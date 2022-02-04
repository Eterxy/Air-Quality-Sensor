String _redirHost;      
String _redirUrl;
struct headerFields{
    String transferEncoding;
    unsigned int contentLength;
    #ifdef EXTRA_FNS
    String contentType;
    #endif
};
headerFields _hF;
struct Response{
    int statusCode;
    String reasonPhrase;
    bool redirected;
    String body;
};
String _Request{/*Replace this comment with the URL*/};
Response _myResponse;
bool _printResponseBody{false};

bool printRedir(void){
    unsigned int httpStatus;
    if (!client.connected()){ //check if connection to host is alive
        Serial.println("Error! Not connected to host. ");
        return false;
    }
    while (client.available())
        client.read();
    DPRINTLN(_Request);  //Set _Request equal to the google sheet URl before running the code

    print(_Request);

    while (client.connected()){
        httpStatus = getResponseStatus(); //gets the case number

        switch (httpStatus)
        {
        case 200:
        case 201:
            {
                fetchHeader();

                #ifdef EXTRA_FNS
                printHeaderFields();
                #endif

                if (_hF.transferEncoding == "chunked")
                    fetchBodyChunked();
                else 
                    fetchBodyUnChunked(_hF.contentLength);
                
                return true;
            }
            break;
        
        case 301:
        case 302://redirection
            {
                if (getLocationURL()){
                    //stop(); // may not be required
                    _myResponse.redirected = true;

                    if (!connect(_redirHost.c_str(), _httpsPort) ) {
                        Serial.println("Connection to re-directed URL failed!");
                        return false;
                    }
                    return printRedir();
                }
                else{
                    Serial.println("Unable to retreive redirection URL!");
                    return false;
                }
            }
            break;
        default:
            Serial.print("Error with request. Response status code: ");
            Serial.println(httpStatus);
            return false;
            break;
        }
    }
    return false;
}

void HTTPSRedirect::createGetRequest(const String& url, const char* host){ //create a new get request using the redirected URL
  _Request =  String("GET ") + url + " HTTP/1.1\r\n" +
                          "Host: " + host + "\r\n" +
                          "User-Agent: ESP8266\r\n" +
                          (_keepAlive ? "" : "Connection: close\r\n") + 
                          "\r\n\r\n";

  return;
}

//void HTTPSRedirect::createPostRequest(const String& url, const char* host, const String& payload){ //create new post request using redirected URL
//  // Content-Length is mandatory in POST requests
//  // Body content will include payload and a newline character
//  unsigned int len = payload.length() + 1;
//  
//  _Request =  String("POST ") + url + " HTTP/1.1\r\n" +
//                          "Host: " + host + "\r\n" +
//                          "User-Agent: ESP8266\r\n" +
//                          (_keepAlive ? "" : "Connection: close\r\n") +
//                          "Content-Type: " + _contentTypeHeader + "\r\n" + 
//                          "Content-Length: " + len + "\r\n" +
//                          "\r\n" +
//                          payload + 
//                          "\r\n\r\n";
//
//  return;
//}

bool getLocationURL(void){
    bool flag;

    flag = find("Location: ");

    if (flag){

        readStringUntil('/');
        readStringUntil('/');

        _redirHost = readStringUntil('/');

        _redirUrl = String('/') + readStringUntil('\n');
    }
    else{
        DPRINT("No Valid 'Location' field found in header!");
    }

    //This function creates a get request using the redirected URL
    createGetRequest(_redirUrl, _redirHost.c_str()); 

    DPRINT("_redirHost: ");
    DPRINTLN(_redirHost);
    DPRINT("_redirUrl: ");
    DPRINTLN(_redirUrl);

    return flag;
}

void fetchHeader(void){
    String line = "";
    int pos{-1},pos2{-1},pos3{-1};
    
    _hF.transferEncoding = "";
    _hF.contentLength = 0;

    #ifdef EXTRA_FNS
    _hF.contentType = "";
    #endif

    while( client.connected() ){
        line = readStringUntil('/');

        DPRINTLN(line);

        if (line == '\r')
            break;
        if (pos < 0){
            pos = line.indexOf("Transfer-Encoding: ");
            if (!pos)
                _hF.transferEncoding = line.substring(19, line.length()-1);
        }
        if (pos2 < 0){
            pos2 = line.indexOf("Content-Length: ");
            if (!pos2)
                //get string and remove trailing '\r'
                _hF.contentType = line.substring(14, line.length()-1);
        }
        #ifdef EXTRA_FNS
        if (pos3 < 0){
            pos3 = line.indexOf("Content-Type: ");
            if (!pos3)
                //get string and remove trailing '\r'
                _hF.contentType = line.substring(14, line.length()-1);
        }
        #endif
    }
    return;
}

void fetchBodyUnChunked(unsigned len){
    String line;
    DPRINTLN("Body:");

    while ((client.connected()) && (len > 0)){
        line = readStringUntil('\n');
        len -= line.length();
        //Content length will include all '\n' terminating characters
        //Decrement once more to account for the '\n' line ending character
        --len;
        if (_printResponseBody)
            Serial.println(line);

        _myResponse.body += line;
        _myResponse.body += '\n';
    }
}

void fetchBodyChunked(void){
    String line;
    int chunkSize;

    while (client.connected()){
    line = readStringUntil('\n');

    // Skip any empty lines
    if (line == "\r")
        continue;
        
    // Chunk sizes are in hexadecimal so convert to integer
    chunkSize = (uint32_t) strtol((const char *) line.c_str(), NULL, 16);
    DPRINT("Chunk Size: ");
    DPRINTLN(chunkSize);

    // Terminating chunk is of size 0
    if (chunkSize == 0)
        break;

    while (chunkSize > 0){
        line = readStringUntil('\n');
        if (_printResponseBody)
        Serial.println(line);

        _myResponse.body += line;
        _myResponse.body += '\n';
        
        chunkSize -= line.length();
        // The line above includes the '\r' character 
        // which is not part of chunk size, so account for it
        --chunkSize;
    }

    // Skip over chunk trailer

    }

    return;

}

unsigned int HTTPSRedirect::getResponseStatus(void){
    // Read response status line
    // ref: https://www.tutorialspoint.com/http/http_responses.htm

    unsigned int statusCode;
    String reasonPhrase;
    String line;

    unsigned int pos = -1;
    unsigned int pos2 = -1;

    // Skip any empty lines
    do{
    line = readStringUntil('\n');
    }while(line.length() == 0);

    pos = line.indexOf("HTTP/1.1 ");
    pos2 = line.indexOf(" ", 9);

    if (!pos){
    statusCode = line.substring(9, pos2).toInt();
    reasonPhrase = line.substring(pos2+1, line.length()-1);
    }
    else{
    DPRINTLN("Error! No valid Status Code found in HTTP Response.");
    statusCode = 0;
    reasonPhrase = "";
    }

    _myResponse.statusCode = statusCode;
    _myResponse.reasonPhrase = reasonPhrase;

    DPRINT("Status code: ");
    DPRINTLN(statusCode);
    DPRINT("Reason phrase: ");
    DPRINTLN(reasonPhrase);

    return statusCode;
}

bool GET(const String& url, const char* host){
    return GET(url, host, _printResponseBody);
}

bool GET(const String& url, const char* host, const bool& disp){
    bool retval;
    bool oldval;

    // set _printResponseBody temporarily to argument passed
    oldval = _printResponseBody;
    _printResponseBody = disp;

    // redirected Host and Url need to be initialized in case a 
    // reConnectFinalEndpoint() request is made after an initial request 
    // which did not have redirection
    _redirHost = host;
    _redirUrl = url;

    InitResponse();

    // Create request packet
    createGetRequest(url, host);

    // Calll request handler
    retval = printRedir();

    _printResponseBody = oldval;
    return retval;
}
//
//bool POST(const String&, const char*, const String&){
//    return POST(url, host, payload, _printResponseBody);
//}
//
//bool POST(const String&, const char*, const String&, const bool&){
//    bool retval;
//    bool oldval;
//
//    // set _printResponseBody temporarily to argument passed
//    oldval = _printResponseBody;
//    _printResponseBody = disp;
//
//    // redirected Host and Url need to be initialized in case a 
//    // reConnectFinalEndpoint() request is made after an initial request 
//    // which did not have redirection
//    _redirHost = host;
//    _redirUrl = url;
//
//    InitResponse();
//
//    // Create request packet
//    createPostRequest(url, host, payload);
//
//    // Call request handler
//    retval = printRedir();
//
//    _printResponseBody = oldval;
//    return retval;
//}