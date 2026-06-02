#include "Server.h"

int main() {
    Server myServer(60001);
    myServer.Run();
    return 0;
}