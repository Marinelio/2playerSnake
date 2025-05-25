#include <raylib.h>
#include <enet/enet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_W 1536
#define SCREEN_H 1152
#define BOX 20
#define MAX_TAIL 100
#define FOODS 50 
#define SERVER_PORT 7777

// Network message types
typedef enum {
    MSG_SNAKE_UPDATE,
    MSG_FOOD_UPDATE,
    MSG_GAME_OVER,
    MSG_RESTART_GAME,
    MSG_PLAYER_JOIN
} MessageType;

typedef struct {
    int x, y;
} Spot;

typedef struct {
    Spot head;
    int size;
    Spot tail[MAX_TAIL];
    Spot go;
    Color color1;
    Color color2;
    int alive;
    int points;
} Snake;

typedef struct {
    Spot place;
    int active;
    Color color;
} Food;

// Network message structures
typedef struct {
    MessageType type;
    Snake snake_data;
    int player_id;
} SnakeMessage;

typedef struct {
    MessageType type;
    Food foods[FOODS];
} FoodMessage;

typedef struct {
    MessageType type;
    int winner;
    int p1_points;
    int p2_points;
} GameOverMessage;

typedef struct {
    MessageType type;
} RestartMessage;

// Network state
typedef struct {
    ENetHost* host;
    ENetPeer* peer;
    int is_server;
    int player_id; // 1 for server/host, 2 for client
    int connected;
} NetworkState;

// Function declarations
void StartSnake(Snake *s, int x, int y, Color c1, Color c2);
void MoveSnake(Snake *s, Food foods[], Snake *other, int *game_over, NetworkState *net);
void ShowSnake(Snake *s);
void MakeFood(Food foods[], Snake *s1, Snake *s2);
void MakeAllFoods(Food foods[], Snake *s1, Snake *s2);
int HitSnake(Spot p, Snake *s);
Color RandomFoodColor();

// Network functions
int InitNetwork(NetworkState *net, int is_server, const char* address);
void CleanupNetwork(NetworkState *net);
void SendSnakeUpdate(NetworkState *net, Snake *snake);
void SendFoodUpdate(NetworkState *net, Food foods[]);
void SendGameOver(NetworkState *net, int winner, int p1_points, int p2_points);
void SendRestart(NetworkState *net);
void HandleNetworkEvents(NetworkState *net, Snake *local_snake, Snake *remote_snake, 
                        Food foods[], int *game_over, int *winner);
void ShowNetworkMenu(NetworkState *net);

int main() {
    InitWindow(SCREEN_W, SCREEN_H, "P2P SNAKE GAME!!!");
    SetTargetFPS(10);
    
    // Initialize ENet
    if (enet_initialize() != 0) {
        fprintf(stderr, "An error occurred while initializing ENet.\n");
        return -1;
    }
    
    NetworkState net = {0};
    Snake snake1, snake2;
    Food foods[FOODS];
    int game_over = 0;
    int winner = 0;
    int in_menu = 1;
    
    // Initialize foods
    for (int i = 0; i < FOODS; i++) {
        foods[i].active = 0;
    }
    
    while (!WindowShouldClose()) {
        if (in_menu) {
            BeginDrawing();
            ClearBackground(BLACK);
            
            DrawText("P2P SNAKE GAME", SCREEN_W/2 - 150, SCREEN_H/2 - 100, 40, WHITE);
            DrawText("Press 'H' to HOST a game", SCREEN_W/2 - 120, SCREEN_H/2 - 40, 20, GREEN);
            DrawText("Press 'J' to JOIN a game", SCREEN_W/2 - 120, SCREEN_H/2 - 10, 20, BLUE);
            DrawText("Press 'L' for LOCAL multiplayer", SCREEN_W/2 - 140, SCREEN_H/2 + 20, 20, YELLOW);
            
            if (net.connected) {
                char status[100];
                sprintf(status, "Connected as Player %d", net.player_id);
                DrawText(status, SCREEN_W/2 - 100, SCREEN_H/2 + 60, 20, WHITE);
                DrawText("Press SPACE to start game", SCREEN_W/2 - 100, SCREEN_H/2 + 90, 20, WHITE);
            }
            
            EndDrawing();
            
            // Menu input handling
            if (IsKeyPressed(KEY_H)) {
                if (InitNetwork(&net, 1, NULL)) {
                    net.player_id = 1;
                    DrawText("Waiting for player 2...", SCREEN_W/2 - 100, SCREEN_H/2 + 60, 20, WHITE);
                }
            } else if (IsKeyPressed(KEY_J)) {
                if (InitNetwork(&net, 0, "127.0.0.1")) { // Connect to localhost
                    net.player_id = 2;
                }
            } else if (IsKeyPressed(KEY_L)) {
                // Local multiplayer mode
                net.connected = 1;
                net.player_id = 0; // Special case for local play
                in_menu = 0;
                
                StartSnake(&snake1, 5, 5, DARKGREEN, GREEN);
                StartSnake(&snake2, 40, 30, DARKBLUE, BLUE);
                MakeAllFoods(foods, &snake1, &snake2);
            }
            
            if (net.connected && IsKeyPressed(KEY_SPACE) && net.player_id != 0) {
                in_menu = 0;
                
                // Initialize snakes based on player ID
                if (net.player_id == 1) {
                    StartSnake(&snake1, 5, 5, DARKGREEN, GREEN);
                    StartSnake(&snake2, 40, 30, DARKBLUE, BLUE);
                    MakeAllFoods(foods, &snake1, &snake2);
                    SendFoodUpdate(&net, foods);
                } else {
                    StartSnake(&snake1, 40, 30, DARKBLUE, BLUE);
                    StartSnake(&snake2, 5, 5, DARKGREEN, GREEN);
                }
            }
            
            // Handle network events even in menu
            if (net.host) {
                HandleNetworkEvents(&net, &snake1, &snake2, foods, &game_over, &winner);
            }
            
            continue;
        }
        
        // Game logic
        if (!game_over) {
            if (net.player_id == 0) {
                // Local multiplayer - handle both snakes
                MoveSnake(&snake1, foods, &snake2, &game_over, &net);
                if (!game_over) {
                    MoveSnake(&snake2, foods, &snake1, &game_over, &net);
                }
            } else if (net.player_id == 1) {
                // Server controls snake1 (green)
                MoveSnake(&snake1, foods, &snake2, &game_over, &net);
                if (!game_over) {
                    SendSnakeUpdate(&net, &snake1);
                }
            } else if (net.player_id == 2) {
                // Client controls snake2 (blue, but displayed as snake1 locally)
                MoveSnake(&snake1, foods, &snake2, &game_over, &net);
                if (!game_over) {
                    SendSnakeUpdate(&net, &snake1);
                }
            }
            
            // Handle network events
            if (net.host) {
                HandleNetworkEvents(&net, &snake1, &snake2, foods, &game_over, &winner);
            }
            
            // Check for game over conditions
            if (game_over && net.player_id != 0) {
                if (!snake1.alive && !snake2.alive) {
                    winner = 0; // Tie
                } else if (!snake1.alive) {
                    winner = (net.player_id == 1) ? 2 : 1;
                } else {
                    winner = (net.player_id == 1) ? 1 : 2;
                }
                
                if (net.player_id == 1) { // Server sends game over
                    SendGameOver(&net, winner, snake1.points, snake2.points);
                }
            } else if (game_over && net.player_id == 0) {
                // Local multiplayer game over logic
                if (!snake1.alive && !snake2.alive) {
                    winner = 0;
                } else if (!snake1.alive) {
                    winner = 2;
                } else {
                    winner = 1;
                }
            }
        }
        
        // Rendering
        BeginDrawing();
        ClearBackground(BLACK);
        
        if (game_over) {
            DrawText("GAME OVER!!!", SCREEN_W/2 - 150, SCREEN_H/2 - 40, 40, RED);
            
            char winner_text[200];
            if (winner == 0) {
                sprintf(winner_text, "TIE GAME! Points: P1=%d P2=%d", snake1.points, snake2.points);
            } else if (winner == 2) {
                if (net.player_id == 0) {
                    sprintf(winner_text, "BLUE WINS!!! Points: P1=%d P2=%d", snake1.points, snake2.points);
                } else {
                    sprintf(winner_text, "PLAYER 2 WINS!!! Points: P1=%d P2=%d", 
                           (net.player_id == 1) ? snake1.points : snake2.points,
                           (net.player_id == 1) ? snake2.points : snake1.points);
                }
            } else {
                if (net.player_id == 0) {
                    sprintf(winner_text, "GREEN WINS!!! Points: P1=%d P2=%d", snake1.points, snake2.points);
                } else {
                    sprintf(winner_text, "PLAYER 1 WINS!!! Points: P1=%d P2=%d",
                           (net.player_id == 1) ? snake1.points : snake2.points,
                           (net.player_id == 1) ? snake2.points : snake1.points);
                }
            }
            
            DrawText(winner_text, SCREEN_W/2 - 250, SCREEN_H/2, 30, YELLOW);
            DrawText("PRESS R TO PLAY AGAIN!!!", SCREEN_W/2 - 180, SCREEN_H/2 + 40, 20, WHITE);
        } else {
            // Draw snakes
            if (snake1.alive) ShowSnake(&snake1);
            if (snake2.alive) ShowSnake(&snake2);
            
            // Draw food
            for (int i = 0; i < FOODS; i++) {
                if (foods[i].active) {
                    DrawRectangle(foods[i].place.x * BOX, foods[i].place.y * BOX, BOX, BOX, foods[i].color);
                }
            }
            
            // Draw scores
            char score1[50], score2[50];
            if (net.player_id == 0) {
                sprintf(score1, "GREEN: %d", snake1.points);
                sprintf(score2, "BLUE: %d", snake2.points);
                DrawText(score1, 10, 10, 20, GREEN);
                DrawText(score2, SCREEN_W - 100, 10, 20, BLUE);
            } else {
                sprintf(score1, "YOU: %d", snake1.points);
                sprintf(score2, "OPPONENT: %d", snake2.points);
                DrawText(score1, 10, 10, 20, WHITE);
                DrawText(score2, SCREEN_W - 150, 10, 20, WHITE);
            }
            
            // Draw controls
            if (net.player_id == 0) {
                DrawText("P1: WASD!!!!", 10, SCREEN_H - 30, 20, GREEN);
                DrawText("P2: ARROWS!!!!", SCREEN_W - 150, SCREEN_H - 30, 20, BLUE);
            } else {
                DrawText("CONTROLS: WASD or ARROWS", 10, SCREEN_H - 30, 20, WHITE);
            }
        }
        
        EndDrawing();
        
        // Restart game
        if (game_over && IsKeyPressed(KEY_R)) {
            if (net.player_id == 1) {
                SendRestart(&net);
            }
            
            // Reset game state
            StartSnake(&snake1, (net.player_id == 2) ? 40 : 5, (net.player_id == 2) ? 30 : 5, 
                      (net.player_id == 2) ? DARKBLUE : DARKGREEN, 
                      (net.player_id == 2) ? BLUE : GREEN);
            StartSnake(&snake2, (net.player_id == 2) ? 5 : 40, (net.player_id == 2) ? 5 : 30, 
                      (net.player_id == 2) ? DARKGREEN : DARKBLUE, 
                      (net.player_id == 2) ? GREEN : BLUE);
            
            for (int i = 0; i < FOODS; i++) {
                foods[i].active = 0;
            }
            
            if (net.player_id == 1 || net.player_id == 0) {
                MakeAllFoods(foods, &snake1, &snake2);
                if (net.player_id == 1) {
                    SendFoodUpdate(&net, foods);
                }
            }
            
            game_over = 0;
            winner = 0;
        }
    }
    
    CleanupNetwork(&net);
    CloseWindow();
    enet_deinitialize();
    return 0;
}

// Network function implementations
int InitNetwork(NetworkState *net, int is_server, const char* address) {
    net->is_server = is_server;
    
    if (is_server) {
        ENetAddress addr;
        addr.host = ENET_HOST_ANY;
        addr.port = SERVER_PORT;
        
        net->host = enet_host_create(&addr, 1, 2, 0, 0);
        if (net->host == NULL) {
            fprintf(stderr, "Failed to create server host\n");
            return 0;
        }
        printf("Server created on port %d\n", SERVER_PORT);
    } else {
        net->host = enet_host_create(NULL, 1, 2, 0, 0);
        if (net->host == NULL) {
            fprintf(stderr, "Failed to create client host\n");
            return 0;
        }
        
        ENetAddress addr;
        enet_address_set_host(&addr, address);
        addr.port = SERVER_PORT;
        
        net->peer = enet_host_connect(net->host, &addr, 2, 0);
        if (net->peer == NULL) {
            fprintf(stderr, "Failed to create peer\n");
            return 0;
        }
    }
    
    return 1;
}

void CleanupNetwork(NetworkState *net) {
    if (net->host) {
        enet_host_destroy(net->host);
        net->host = NULL;
    }
}

void SendSnakeUpdate(NetworkState *net, Snake *snake) {
    if (!net->host || !net->connected) return;
    
    SnakeMessage msg;
    msg.type = MSG_SNAKE_UPDATE;
    msg.snake_data = *snake;
    msg.player_id = net->player_id;
    
    ENetPacket *packet = enet_packet_create(&msg, sizeof(SnakeMessage), ENET_PACKET_FLAG_RELIABLE);
    
    if (net->is_server) {
        enet_host_broadcast(net->host, 0, packet);
    } else {
        enet_peer_send(net->peer, 0, packet);
    }
}

void SendFoodUpdate(NetworkState *net, Food foods[]) {
    if (!net->host || !net->connected) return;
    
    FoodMessage msg;
    msg.type = MSG_FOOD_UPDATE;
    memcpy(msg.foods, foods, sizeof(Food) * FOODS);
    
    ENetPacket *packet = enet_packet_create(&msg, sizeof(FoodMessage), ENET_PACKET_FLAG_RELIABLE);
    
    if (net->is_server) {
        enet_host_broadcast(net->host, 0, packet);
    } else {
        enet_peer_send(net->peer, 0, packet);
    }
}

void SendGameOver(NetworkState *net, int winner, int p1_points, int p2_points) {
    if (!net->host || !net->connected) return;
    
    GameOverMessage msg;
    msg.type = MSG_GAME_OVER;
    msg.winner = winner;
    msg.p1_points = p1_points;
    msg.p2_points = p2_points;
    
    ENetPacket *packet = enet_packet_create(&msg, sizeof(GameOverMessage), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(net->host, 0, packet);
}

void SendRestart(NetworkState *net) {
    if (!net->host || !net->connected) return;
    
    RestartMessage msg;
    msg.type = MSG_RESTART_GAME;
    
    ENetPacket *packet = enet_packet_create(&msg, sizeof(RestartMessage), ENET_PACKET_FLAG_RELIABLE);
    enet_host_broadcast(net->host, 0, packet);
}

void HandleNetworkEvents(NetworkState *net, Snake *local_snake, Snake *remote_snake, 
                        Food foods[], int *game_over, int *winner) {
    ENetEvent event;
    
    while (enet_host_service(net->host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                printf("Player connected\n");
                net->connected = 1;
                if (net->is_server) {
                    net->peer = event.peer;
                }
                break;
                
            case ENET_EVENT_TYPE_RECEIVE: {
                MessageType *msg_type = (MessageType*)event.packet->data;
                
                switch (*msg_type) {
                    case MSG_SNAKE_UPDATE: {
                        SnakeMessage *msg = (SnakeMessage*)event.packet->data;
                        if (msg->player_id != net->player_id) {
                            *remote_snake = msg->snake_data;
                        }
                        break;
                    }
                    
                    case MSG_FOOD_UPDATE: {
                        FoodMessage *msg = (FoodMessage*)event.packet->data;
                        memcpy(foods, msg->foods, sizeof(Food) * FOODS);
                        break;
                    }
                    
                    case MSG_GAME_OVER: {
                        GameOverMessage *msg = (GameOverMessage*)event.packet->data;
                        *game_over = 1;
                        *winner = msg->winner;
                        break;
                    }
                    
                    case MSG_RESTART_GAME: {
                        // Handle restart from server
                        *game_over = 0;
                        *winner = 0;
                        break;
                    }
                }
                
                enet_packet_destroy(event.packet);
                break;
            }
            
            case ENET_EVENT_TYPE_DISCONNECT:
                printf("Player disconnected\n");
                net->connected = 0;
                break;
        }
    }
}

// Original game function implementations (modified for networking)
void StartSnake(Snake *s, int x, int y, Color c1, Color c2) {
    s->head.x = x;
    s->head.y = y;
    s->size = 1;
    s->go.x = 1; 
    s->go.y = 0;
    s->color1 = c1;
    s->color2 = c2;
    s->alive = 1;
    s->points = 0;
}

void MoveSnake(Snake *s, Food foods[], Snake *other, int *game_over, NetworkState *net) {
    if (!s->alive) return;
    
    // Input handling based on network role
    int use_wasd = (net->player_id == 0 && s->color1.g > s->color1.b) || 
                   (net->player_id == 1) || 
                   (net->player_id == 2);
    int use_arrows = (net->player_id == 0 && s->color1.g < s->color1.b);
    
    if (use_wasd) {
        if (IsKeyPressed(KEY_W) && s->go.y == 0) {
            s->go.x = 0; s->go.y = -1;
        }
        if (IsKeyPressed(KEY_S) && s->go.y == 0) {
            s->go.x = 0; s->go.y = 1;
        }
        if (IsKeyPressed(KEY_A) && s->go.x == 0) {
            s->go.x = -1; s->go.y = 0;
        }
        if (IsKeyPressed(KEY_D) && s->go.x == 0) {
            s->go.x = 1; s->go.y = 0;
        }
    }
    
    if (use_arrows) {
        if (IsKeyPressed(KEY_UP) && s->go.y == 0) {
            s->go.x = 0; s->go.y = -1;
        }
        if (IsKeyPressed(KEY_DOWN) && s->go.y == 0) {
            s->go.x = 0; s->go.y = 1;
        }
        if (IsKeyPressed(KEY_LEFT) && s->go.x == 0) {
            s->go.x = -1; s->go.y = 0;
        }
        if (IsKeyPressed(KEY_RIGHT) && s->go.x == 0) {
            s->go.x = 1; s->go.y = 0;
        }
    }
    
    // Network players can also use arrows
    if (net->player_id != 0) {
        if (IsKeyPressed(KEY_UP) && s->go.y == 0) {
            s->go.x = 0; s->go.y = -1;
        }
        if (IsKeyPressed(KEY_DOWN) && s->go.y == 0) {
            s->go.x = 0; s->go.y = 1;
        }
        if (IsKeyPressed(KEY_LEFT) && s->go.x == 0) {
            s->go.x = -1; s->go.y = 0;
        }
        if (IsKeyPressed(KEY_RIGHT) && s->go.x == 0) {
            s->go.x = 1; s->go.y = 0;
        }
    }
    
    // Move snake body
    for (int i = s->size; i > 0; i--) {
        s->tail[i] = s->tail[i-1];
    }
    s->tail[0] = s->head;
    
    // Move head
    s->head.x += s->go.x;
    s->head.y += s->go.y;
    
    // Wrap around screen
    if (s->head.x < 0) s->head.x = SCREEN_W / BOX - 1;
    if (s->head.x >= SCREEN_W / BOX) s->head.x = 0;
    if (s->head.y < 0) s->head.y = SCREEN_H / BOX - 1;
    if (s->head.y >= SCREEN_H / BOX) s->head.y = 0;
    
    // Check food collision
    for (int i = 0; i < FOODS; i++) {
        if (foods[i].active && s->head.x == foods[i].place.x && s->head.y == foods[i].place.y) {
            if (s->size < MAX_TAIL) {
                s->size++;
            }
            s->points++;
            foods[i].active = 0;
            
            // Only server generates new food
            if (net->player_id == 1 || net->player_id == 0) {
                MakeFood(foods, s, other);
                if (net->player_id == 1) {
                    SendFoodUpdate(net, foods);
                }
            }
        }
    }
    
    // Check self collision
    for (int i = 1; i < s->size; i++) {
        if (s->head.x == s->tail[i].x && s->head.y == s->tail[i].y) {
            s->alive = 0;
            *game_over = 1;
            break;
        }
    }
    
    // Check collision with other snake
    if (other->alive) {
        if (s->head.x == other->head.x && s->head.y == other->head.y) {
            s->alive = 0;
            other->alive = 0;
            *game_over = 1;
        }
        
        for (int i = 0; i < other->size; i++) {
            if (s->head.x == other->tail[i].x && s->head.y == other->tail[i].y) {
                s->alive = 0;
                *game_over = 1;
                break;
            }
        }
    }
}

void ShowSnake(Snake *s) {
    for (int i = 0; i < s->size; i++) {
        DrawRectangle(s->tail[i].x * BOX, s->tail[i].y * BOX, BOX, BOX, s->color2);
    }
    DrawRectangle(s->head.x * BOX, s->head.y * BOX, BOX, BOX, s->color1);
}

void MakeFood(Food foods[], Snake *s1, Snake *s2) {
    int food_index = -1;
    for (int i = 0; i < FOODS; i++) {
        if (!foods[i].active) {
            food_index = i;
            break;
        }
    }
    
    if (food_index == -1) return;
    
    int max_tries = 100;
    int tried = 0;
    
    while (tried < max_tries) {
        tried++;
        
        int ok = 1;
        Spot new_food;
        
        new_food.x = GetRandomValue(0, SCREEN_W / BOX - 1);
        new_food.y = GetRandomValue(0, SCREEN_H / BOX - 1);
        
        if (HitSnake(new_food, s1) || HitSnake(new_food, s2)) {
            ok = 0;
            continue;
        }
        
        for (int i = 0; i < FOODS; i++) {
            if (foods[i].active && foods[i].place.x == new_food.x && foods[i].place.y == new_food.y) {
                ok = 0;
                break;
            }
        }
        
        if (ok) {
            foods[food_index].place = new_food;
            foods[food_index].active = 1;
            foods[food_index].color = RandomFoodColor();
            break;
        }
    }
}

void MakeAllFoods(Food foods[], Snake *s1, Snake *s2) {
    for (int i = 0; i < FOODS; i++) {
        if (!foods[i].active) {
            int max_tries = 100;
            int tried = 0;
            
            while (tried < max_tries) {
                tried++;
                
                int ok = 1;
                Spot new_food;
                
                new_food.x = GetRandomValue(0, SCREEN_W / BOX - 1);
                new_food.y = GetRandomValue(0, SCREEN_H / BOX - 1);
                
                if (HitSnake(new_food, s1) || HitSnake(new_food, s2)) {
                    ok = 0;
                    continue;
                }
                
                for (int j = 0; j < FOODS; j++) {
                    if (foods[j].active && foods[j].place.x == new_food.x && foods[j].place.y == new_food.y) {
                        ok = 0;
                        break;
                    }
                }
                
                if (ok) {
                    foods[i].place = new_food;
                    foods[i].active = 1;
                    foods[i].color = RandomFoodColor();
                    break;
                }
            }
        }
    }
}

int HitSnake(Spot p, Snake *s) {
    if (p.x == s->head.x && p.y == s->head.y) {
        return 1;
    }
    
    for (int i = 0; i < s->size; i++) {
        if (p.x == s->tail[i].x && p.y == s->tail[i].y) {
            return 1;
        }
    }
    
    return 0;
}

Color RandomFoodColor() {
    int c = GetRandomValue(0, 7);
    switch(c) {
        case 0: return RED;
        case 1: return ORANGE;
        case 2: return YELLOW;
        case 3: return PINK;
        case 4: return PURPLE;
        case 5: return GOLD;
        case 6: return SKYBLUE;
        default: return LIME;
    }
}
