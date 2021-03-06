#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <netinet/in.h>
#include <pthread.h>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include "network/network.h"
#include "rpc.h"

static int bindSockfd;
static std::vector<Server*> serverStore;

int moveToArgs(exeMsg *msg, void **args, int *argTypes)
{
    size_t argTypesLen = getArgTypesLen(argTypes);
    int numArgs = (argTypesLen/INT_SIZE) - 1;
    size_t lengths[numArgs];
    int lenArray = 0;
    int dataType;
    for(int i = 0; i < numArgs; i++)
    {
        lenArray = argTypes[i] & 0xffff;
        if(lenArray == 0)
            lenArray = 1;
        dataType = (argTypes[i] >> 16) & 0xff;
        lengths[i] = getDataTypeLen(dataType)*lenArray;
    }
    for(int i = 0; i < numArgs; i++)
    {
        memcpy(args[i], msg->args[i], lengths[i]);
    }
    return 1;
}

int sendExecuteToServer(char *IP, int p, message msg, void **args, int *argTypes)
{
    int servSockfd;
    void *rcvdMsg;
    struct addrinfo *serverInfo;
    char *port = (char*)malloc(INT_SIZE);
    std::stringstream out;
    out << p;
    strcpy(port, out.str().c_str());
    serverInfo = getAddrInfo(IP, port);
    servSockfd = getSocket();
    int status;
    if(servSockfd > 0)
    {
        if((status = connectSocket(servSockfd, serverInfo)) > 0)
        {
            rcvdMsg = sendRecvBinder(servSockfd, msg);
        }
        else
        {
            return status;
        }
        if(rcvdMsg == 0)
        {
            printf("Execute Request failed\n");
            return SERVER_NOT_FOUND;
        }
        else
        {
            switch(((termMsg*)rcvdMsg)->type)
            {
                case EXECUTE_SUCCESS:
                    printf("EXECUTE REQUEST SUCCESS\n");
                    moveToArgs((exeMsg*)rcvdMsg, args, argTypes);
                    free(rcvdMsg);
                    return 0;
                case EXECUTE_FAILURE:
                    int rc = ((sucFailMsg*)rcvdMsg)->reason;
                    free(rcvdMsg);
                    return rc;
            }
        }
    }
    return 1;
}

int openConnBinder()
{
    struct addrinfo *binderInfo;
    char *binderIP = getenv("BINDER_ADDRESS");
    char *binderPort = getenv("BINDER_PORT");
    binderInfo = getAddrInfo(binderIP, binderPort);
    bindSockfd = getSocket();
    if(bindSockfd > 0)
    {
        if(connectSocket(bindSockfd, binderInfo) > 0)
        {
            return 1;
        }
    }
    
    return 0;
}

int compare(location a, location b)
{
    if (strcmp(a.IP, b.IP) == 0 && a.port == b.port)
        return 1;
    else
        return 0;
}

int insertIntoCache(char *IP, int port, skeleArgs *functions)
{
    skeleArgs *key;
    std::vector<Server*>::iterator itServer;
    key = createFuncArgs(functions->name, functions->argTypes);
    location *value;
    value = createLocation(IP, port);
    std::pair<std::set<skeleArgs*, cmp_skeleArgs>::iterator,bool> ret;
    if(key != 0 && value != 0)
    {
        for(itServer = serverStore.begin(); itServer != serverStore.end(); itServer++)
        {
            if(compare(*(*itServer)->loc, *value))
            {
                (*((*itServer)->functions)).insert(key);
                break;
            }
        }
        if(itServer == serverStore.end())
        {
            Server *newServer = new Server;
            newServer->loc = value;
            newServer->functions = new std::set<skeleArgs*, cmp_skeleArgs>;
            ret = (*(newServer->functions)).insert(key);
            serverStore.insert(serverStore.begin(), newServer);
        }
    }
    return 1;
}

void **retrieveFromCache(skeleArgs *funct)
{
    void **value = (void**)malloc(100*VOID_SIZE);
    std::vector<Server*>::iterator itServer;
    std::set<skeleArgs*, cmp_skeleArgs>::iterator itFuncs;
    int found = 0;
    int j = 0;
    if(funct != 0)
    {
        for(itServer = serverStore.begin(); itServer != serverStore.end(); itServer++)
        {
            itFuncs = (*(*itServer)->functions).find(funct);
            if(itFuncs != (*(*itServer)->functions).end())
            {
                found = 1;
                value[j] = (*itServer)->loc;
                j++;
            }
        }
        //value[j] = (location*)malloc(VOID_SIZE);
        value[j] = 0;
    }
    if (found == 1)
        return value;
    return 0;
}
//location *retrieveFromCache(skeleArgs *funct)
//{
//    location *value;
//    std::vector<Server*>::iterator itServer;
//    std::set<skeleArgs*, cmp_skeleArgs>::iterator itFuncs;
//    int found = 0;
//    int j = 0;
//    if(funct != 0)
//    {
//        for(itServer = serverStore.begin(); itServer != serverStore.end(); itServer++)
//        {
//            itFuncs = (*(*itServer)->functions).find(funct);
//            if(itFuncs != (*(*itServer)->functions).end())
//            {
//                found = 1;
//                value = (*itServer)->loc;
//            }
//        }
//    }
//    if (found == 1)
//        return value;
//    return 0;
//}

int rpcCall(char *name, int *argTypes, void **args)
{
    message exec_msg;
    openConnBinder();
    message msg;
    msg = createLocReqMsg(LOC_REQUEST, name, argTypes);
    void *rcvdMsg = sendRecvBinder(bindSockfd, msg);
    if(rcvdMsg == 0)
    {
        printf("Location Request failed\n");
        printf("BINDER_NOT_FOUND\n");
        return BINDER_NOT_FOUND;
    }
    else
    {
        switch(((termMsg*)rcvdMsg)->type)
        {
            case LOC_SUCCESS:
                printf("LOCATION REQUEST SUCCESS\n");
                exec_msg = createExeSucMsg(EXECUTE, name, argTypes, args);
                return sendExecuteToServer(((locSucMsg*)rcvdMsg)->IP, ((locSucMsg*)rcvdMsg)->port, exec_msg, args, argTypes);
            case LOC_FAILURE:
                int rc = ((sucFailMsg*)rcvdMsg)->reason;
                free(rcvdMsg);
                return rc;
        }
    }
    return 1;
}

int rpcCacheCall(char * name, int * argTypes, void ** args)
{
    openConnBinder();
    skeleArgs *functions;
    void **loc;
    message m, msg, exec_msg;
    functions = createFuncArgs(name, argTypes);
    loc = retrieveFromCache(functions);
    bool done = false;
    if (loc == 0)
    {
        msg = createLocReqMsg(LOC_CACHE_REQUEST, name, argTypes);
        if(sendToEntity(bindSockfd, msg) == 0)
        {
            printf("Location Request failed\n");
            printf("BINDER_NOT_FOUND\n");
            return BINDER_NOT_FOUND;
        }
        else
        {
            while(!done)
            {
                void *rcvdMsg = recvFromEntity(bindSockfd);
                if(rcvdMsg == 0)
                {
                    printf("BINDER_NOT_FOUND\n");
                    return BINDER_NOT_FOUND;
                }
                switch(((termMsg*)rcvdMsg)->type)
                {
                    case LOC_CACHE_SUCCESS:
                        printf("LOCATION REQUEST SUCCESS\n");
                        insertIntoCache(((locSucMsg*)rcvdMsg)->IP, ((locSucMsg*)rcvdMsg)->port, functions);
                        break;
                    case LOC_CACHE_FAILURE:
                        int rc = ((sucFailMsg*)rcvdMsg)->reason;
                        free(rcvdMsg);
                        if(rc != END)
                            return rc;
                        done = true;
                }
            }
        }
        loc = retrieveFromCache(functions);
    }
    exec_msg = createExeSucMsg(EXECUTE, name, argTypes, args);
    //return sendExecuteToServer(loc->IP, loc->port, exec_msg, args, argTypes);
    int ret;
    for(int i = 0; loc[i] != 0; i++)
    {
        ret = sendExecuteToServer(((location*)loc[i])->IP, ((location*)loc[i])->port, exec_msg, args, argTypes);
        if(ret == 0)
            return 0;
    }
}

int rpcTerminate()
{
    message msg;
    openConnBinder();
    msg = createTermMsg(TERMINATE);
    if(sendToEntity(bindSockfd, msg) == 0)
    {
        printf("BINDER_NOT_FOUND");
        return BINDER_NOT_FOUND;
    }
    return 1;
}
