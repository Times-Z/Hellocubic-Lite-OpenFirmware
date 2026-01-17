#ifndef API_H
#define API_H

#include "web/Webserver.h"

void registerApiEndpoints(Webserver* webserver);
void handleOtaUpload(Webserver* webserver, int mode);
void handleOtaFinished(Webserver* webserver);
void handleReboot(Webserver* webserver);

void handleGifUpload(Webserver* webserver);
void handleListGifs(Webserver* webserver);
void handlePlayGif(Webserver* webserver);
void handleStopGif(Webserver* webserver);

#endif  // API_H
