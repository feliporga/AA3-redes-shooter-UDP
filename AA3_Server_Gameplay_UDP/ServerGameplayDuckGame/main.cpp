#include "Server.h"

short main() {
    Server myServer(60001);
    myServer.Run();
    return 0;
}