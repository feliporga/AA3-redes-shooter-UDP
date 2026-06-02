#include "Server.h"

short main() {
    Server myServer(SERVER_TCP_PORT);
    myServer.Run();
    return 0;
}