#include <algorithm>
#include <cstddef>
#include <ctime>
#include <map>
#include <ncurses.h>
#include <mutex>
#include <deque>
#include <cstdlib>
#include <cstring>
#include <enet/enet.h>
#include <enet/types.h>
#include <iostream>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <sstream>

std::mutex chat;
std::deque<std::string> logg;
const int maxChatLines = 100;
std::string chatBorder = "[{uname}]: ";

std::map<std::string,std::string> names;

WINDOW* chatWin;
WINDOW* inputWin;

std::string last = "";

ENetAddress addy;
bool running = false;
std::string username = "anon";
ENetHost* host;
ENetPeer* peer;
bool isServer = false;

void sendMessage(std::string message);

bool processCommand(std::string msg){
    int p = chatBorder.length();
    if(p >= msg.length() || msg[p] != '/'){
        return false;
    }
    p++;

    std::istringstream iss(msg.substr(p));
    std::string token;
    std::vector<std::string> args;

    while(iss >> token){
        args.push_back(token);
    }

    if(args.empty()) return false;

    std::string command = args[0];


    // CLIENT COMMANDS
    if(command=="test"){
        sendMessage("my goat");
        if(args.size()>=2&&args[1] == "balls"){
            sendMessage("my go to is");
        }
    }

    // SERVER-ONLY COMMANDS
    if(isServer){
        if(command=="test2"){
            sendMessage("my goatiest auu");
            if(args.size()>=2&&args[1] == "uh"){
                sendMessage("hawk two uh");
            }
        }
    }

    args.erase(args.begin());
    return true;
}

ENetHost* startServer(){
    isServer = true;
    host = enet_host_create(&addy,32,2,0,0);

    if(host==nullptr){
        std::cerr << "Something went wrong when initializing ENet server, please try again.";
        endwin();
        exit(EXIT_FAILURE);
    }

    return host;
}

ENetPeer* connectClient(){
    host = enet_host_create(NULL,1,2,0,0);
    peer = enet_host_connect(host,&addy,2,0);

    if(!peer){
        std::cerr << "Failed to connect to server.\n";
        endwin();
        exit(EXIT_FAILURE);
    }

    ENetEvent event;
    if(enet_host_service(host,&event,3000)>0&&event.type==ENET_EVENT_TYPE_CONNECT){
    }else{
        std::cerr << "Failed to connect to server.\n";
        enet_peer_reset(peer);
        endwin();
        exit(EXIT_FAILURE);
    }

    return peer;
}

void broadcastMessage(const std::string& msg, ENetPeer* sender = nullptr){
    if(isServer){
        for(size_t i = 0; i < host->peerCount; i++){
            ENetPeer* p = &host->peers[i];
            if(p->state == ENET_PEER_STATE_CONNECTED && p != sender){
                ENetPacket* packet = enet_packet_create(msg.c_str(), msg.size() + 1, ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(p, 0, packet);
            }
        }
        enet_host_flush(host);
    }
}

void redrawChat(){
    std::lock_guard<std::mutex> lock(chat);
    werase(chatWin);

    int maxWidth = COLS - 2;
    int maxHeight = LINES - 4;
    int printedLines = 0;
    int start = 0;

    int totalWrappedLines = 0;
    for(int i = 0; i < logg.size(); i++){
        int wrapped = (logg[i].length() + maxWidth - 1) / maxWidth;
        totalWrappedLines += wrapped;
    }
    int skipLines = totalWrappedLines > maxHeight ? totalWrappedLines - maxHeight : 0;

    for(int i = 0; i < logg.size(); i++){
        int lineLen = logg[i].length();
        int wrappedLines = (lineLen + maxWidth - 1) / maxWidth;

        if(skipLines >= wrappedLines){
            skipLines -= wrappedLines;
            continue;
        }

        for(int w = skipLines; w < wrappedLines; w++){
            if(printedLines >= maxHeight) break;
            int startPos = w * maxWidth;
            int len = std::min(maxWidth, lineLen - startPos);
            std::string sub = logg[i].substr(startPos, len);
            mvwprintw(chatWin, printedLines, 0, "%s", sub.c_str());
            printedLines++;
        }
        skipLines = 0;

        if(printedLines >= maxHeight) break;
    }

    wrefresh(chatWin);
    wrefresh(inputWin);
}

void sendMessage(std::string message){
    {
        std::lock_guard<std::mutex> lock(chat);
        logg.push_back(message);
        if(logg.size() > maxChatLines){
            logg.pop_front();
        }
    }

    redrawChat();

    if(!processCommand(message)){
        if(isServer){
            broadcastMessage(message);
        }else{
            ENetPacket* packet = enet_packet_create(message.c_str(), message.size() + 1, ENET_PACKET_FLAG_RELIABLE);
            enet_peer_send(peer, 0, packet);
            enet_host_flush(host);
        }
    }
}

void handleEvent(ENetEvent event){
    switch(event.type){
        case ENET_EVENT_TYPE_CONNECT:{
            break;
        }
        case ENET_EVENT_TYPE_RECEIVE:{
            if(event.channelID==1){
                char ip[32];
                enet_address_get_host_ip(&event.peer->address, ip, sizeof(ip));
                names[ip] = std::string((char*)event.packet->data);
                sendMessage(names[ip]+" has joined the room.");
                break;
            }
            std::string msg((char*)event.packet->data);
            {
                std::lock_guard<std::mutex> lock(chat);
                logg.push_back(msg);
                if(logg.size() > maxChatLines){
                    logg.pop_front();
                }
            }
            redrawChat();

            if(isServer){
                broadcastMessage(msg, event.peer);
            }
            break;
        }
        case ENET_EVENT_TYPE_DISCONNECT:{
            char ip[32];
            enet_address_get_host_ip(&event.peer->address, ip, sizeof(ip));
            sendMessage(names[ip]+" has left the room.");
            break;
        }
        default:{
            break;
        }
    }
}

void die(){
    endwin();
    enet_peer_disconnect_now(peer,ENET_PEER_STATE_DISCONNECTING);
    enet_deinitialize();
}

void inputThread(ENetHost* host){
    std::string buffer;
    int ch;
    keypad(inputWin, TRUE);

    while(running){
        buffer.clear();
        int cursorPos = 0;

        while(true){
            werase(inputWin);
            mvwprintw(inputWin, 0, 0, "> ");

            int maxCols = COLS - 3;
            int maxLines = 3;

            std::vector<std::string> lines;
            for(size_t i=0; i<buffer.size();){
                lines.push_back(buffer.substr(i, maxCols));
                i += maxCols;
            }
            int startLine = lines.size() > maxLines ? lines.size() - maxLines : 0;
            for(int i = startLine; i < lines.size(); i++){
                mvwprintw(inputWin, i - startLine, 2, "%s", lines[i].c_str());
            }
            int cursorLine = (cursorPos / maxCols) - startLine;
            int cursorCol = (cursorPos % maxCols) + 2;
            wmove(inputWin, cursorLine, cursorCol);
            wrefresh(inputWin);

            ch = wgetch(inputWin);

            if(ch == '\n' || ch == '\r' || ch == KEY_ENTER){
                break;
            }else if(ch == 14){ 
                buffer.insert(buffer.begin() + cursorPos, '\n');
                cursorPos++;
            }else if(ch == KEY_BACKSPACE || ch == 127 || ch == 8){
                if(cursorPos > 0){
                    buffer.erase(cursorPos-1, 1);
                    cursorPos--;
                }
            }else if(isprint(ch) || ch == '\t'){
                buffer.insert(buffer.begin() + cursorPos, ch);
                cursorPos++;
            }else if(ch == KEY_LEFT){
                if(cursorPos > 0) cursorPos--;
            }else if(ch == KEY_RIGHT){
                if(cursorPos < buffer.size()) cursorPos++;
            }else if(ch == KEY_UP){
                buffer = last;
                cursorPos = last.size();
            }else if(isprint(ch)){
                buffer.insert(buffer.begin() + cursorPos, ch);
                cursorPos++;
            }
        }

        if(!buffer.empty()){
            last = buffer;
            std::string message = chatBorder + buffer;
            sendMessage(message);
        }

        werase(inputWin);
        mvwprintw(inputWin, 0, 0, "> ");
        wmove(inputWin, 0, 2);
        mvhline(LINES - 4, 0, '_', COLS);
        wrefresh(inputWin);
        curs_set(1);
    }
}

void eventLoop(ENetHost* host){
    running = true;
    ENetEvent event;
    std::thread input(inputThread, host);
    while(running){
        while(enet_host_service(host,&event,1000)>0){
            handleEvent(event);
        }
    }
    if(input.joinable()){
        input.join();
    }
}

std::map<std::string,std::string> loadConf(const std::string& filename){
    std::ifstream file(filename);
    std::map<std::string,std::string> conf;
    std::string line;
    while(std::getline(file,line)){
        size_t eq = line.find("=");
        if(eq!=std::string::npos){
            std::string key = line.substr(0,eq);
            std::string val = line.substr(eq+1);
            conf[key] = val;
        }
    }
    return conf;
}

void applyConf(std::map<std::string,std::string> conf){
    if(conf.count("username")){
        username = conf["username"];
    }

    if(conf.count("ip")){
        if(enet_address_set_host(&addy,conf["ip"].c_str())!=0){
            std::cerr << "Invalid IP, try 'localhost' if you're hosting on your own system.\n";
        }
    }
}

int main(int argc, char* argv[]){
    int type = 2;
    applyConf(loadConf("vchat.conf"));
    if(argc > 1){
        for(int i=1;i<argc;i++){
            if(std::strcmp(argv[i],"-h")==0){
                std::cout << "Usage:\n-i : sets the (-i)p (ex: -i 128.0.0.1)\n-p : sets the (-p)ort (ex: -p 25565)\n-t : the (-t)ype of hosting you're doing (ex: -t s)\n\ts : (s)erver\n\tc : (c)lient\n-u : sets your (-u)sername.\n\tUsage: -u 'username'\n-b : adds a custom (-b)order to your name\n\tUsage: use whatever you like, but use {uname} to add your username, also please only add one. Also use '' to encapsule it.\n\tDefault: '[{uname}]: '\n-h : shows this (the (-h)elp) menu (no way) (ex: -h)\n\nExample of full command:\n'vchat -u Username -i 128.0.0.1 -p 25565 -t s'\n";
                return 0;
            }else if(std::strcmp(argv[i],"-i")==0){
                if(enet_address_set_host(&addy, argv[i += 1])!=0){
                    std::cerr << "Invalid IP, try 'localhost' if you're hosting on your own system.\n";
                }
            }else if(std::strcmp(argv[i],"-p")==0){
                addy.port = std::stoi(argv[i += 1]);
            }else if(std::strcmp(argv[i],"-t")==0){
                char c = *argv[i+=1];
                type = c=='s'?0:c=='c'?1:2;
            }else if(std::strcmp(argv[i],"-u")==0){
                username = (argv[i += 1]);
            }else if(std::strcmp(argv[i],"-b")==0){
                size_t s = std::string(argv[i+=1]).find("{uname}");
                if(s!=std::string::npos){
                    chatBorder = argv[i];
                }
            }
        }
    }

    {
        size_t s = std::string(chatBorder).find("{uname}");
        if(s!=std::string::npos){
            chatBorder.replace(s,7,username);
        }
    }

    initscr();
    cbreak();
    noecho();
    curs_set(1);
    chatWin = newwin(LINES - 4, COLS-6, 0, 15);
    inputWin = newwin(3, COLS-6, LINES - 3, 15);
    scrollok(chatWin,true);
    keypad(inputWin, TRUE);
    mvhline(LINES - 4, 14,'_', COLS);
    mvvline(0,14,ACS_VLINE,LINES);
    refresh();

    wrefresh(chatWin);
    wrefresh(inputWin);

    if(enet_initialize()!=0){
        std::cerr << "Failed to initialize ENet, try restarting VChat";
        return 1;
    }

    std::atexit(die);

    char ip[32];
    enet_address_get_host_ip(&addy, ip, sizeof(ip));

    if(type==0){
        startServer();
        sendMessage("Server started on ip: "+std::string(ip)+":"+std::to_string(addy.port));
        eventLoop(host);
    }else if(type==1){
        connectClient();
        ENetPacket* p = enet_packet_create(username.c_str(),username.length(),ENET_PACKET_FLAG_NO_ALLOCATE);
        enet_peer_send(peer,1,p);
        eventLoop(peer->host);
    }

    endwin();

    return 0;
}