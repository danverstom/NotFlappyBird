/**
 * Game has constantly rising platform
 *
 * - Physics engine for the ball and paddle
 * - Procedurally generated world with "difficulty" parameter
 *
 * There are monsters that spawn. You have to hit them with the ball. If they get to the bottom of the screen then you
 * die. If you lose the ball then you die.
 *
 *
 * The cause of the bad rendering is because the object given in the text file is not square
 */

#include <stdio.h>
#include <unistd.h>
#include "windows.h"
#include <sys/time.h>
#include <stdbool.h>
#include "conio.h"
#include "math.h"

#define ESC "\x1b"
#define CSI "\x1b["

#define SCREEN_WIDTH    300
#define SCREEN_HEIGHT   80
#define FRAME_RATE      144     // frames per second
#define SCORE_COUNTER_DIGITS 5

enum EntityType {
    PLAYER,
    MONSTER,
    BIRD,
    OBSTACLE,
    OVERLAY
};

struct EntityView {
    int origin_x;
    int origin_y;
    int width;
    int height;
    size_t display_size;
    char *display;
};


struct Entity {
    int x;
    int y;
    enum EntityType type;
    unsigned int num_views;
    unsigned int current_view;
    struct EntityView views[10];
    long long int last_update_time;
    bool visible;
};

struct Obstacle {
    int x;
    int y;
    int gap_size;
    struct Entity top_entity;
    struct Entity bottom_entity;
    bool score_collected;
};

struct Bird {
    struct Entity entity;
    float velocity; // current velocity in pixels per tick
};

struct ScoreCounter {
    struct Entity digits[SCORE_COUNTER_DIGITS];
    int score;
    int x;
    int y;
};


struct DisplayState {
    char current_frame[SCREEN_WIDTH][SCREEN_HEIGHT];
    char next_frame[SCREEN_WIDTH][SCREEN_HEIGHT];
    long long last_frame_time;
};

enum ScreenType {
    TITLE_SCREEN,
    GAME_SCREEN,
    GAME_OVER_SCREEN
};

struct GameState {
    int player_x;
    int player_y;
    struct Bird bird;
    size_t entity_count;
    struct Entity *entities[25];
    size_t obstacle_count;
    struct Obstacle obstacles[25];
    struct Entity start_button;
    enum ScreenType screen_type;
    struct Entity title_text;
    int score;
    struct ScoreCounter score_counter;
};


struct PeriodicTimer {
    long long int period;
    long long int last_trigger_time;

    void (*callback)(struct GameState *game_state);
};

void set_cursor(int x, int y);

void cls();

void get_viewport_size(int *rows, int *columns);

void wait_for_user_to_resize_console();

long long millis();

void update_display(struct DisplayState *display_state);

void render_next_frame(struct DisplayState *display_state, struct GameState *game_state);

void render_entity(struct Entity *entity, char frame[SCREEN_WIDTH][SCREEN_HEIGHT]);

struct Entity create_entity(enum EntityType type);

void add_entity_view_from_file(struct Entity *entity, char *filename);

void next_entity_view(struct Entity *entity);

void register_entity(struct GameState *game_state, struct Entity *entity);

void create_obstacle(struct GameState *game_state, int x, int y, int gap_size);

void update_obstacle(struct Obstacle *obstacle, struct GameState *game_state);

void run_periodic_timers(struct GameState *game_state, struct PeriodicTimer periodic_timers[], size_t periodic_timer_count);

void create_score_counter(struct GameState *game_state, int x, int y);
void update_score_counter(struct GameState *game_state);

// game functions
void scroll_world(struct GameState *game_state);

void animate_bird(struct GameState *game_state);

void game_tick(struct GameState *game_state);

bool check_collision(struct Entity *entity1, struct Entity *entity2);

void start_game(struct GameState *game_state);
void end_game(struct GameState *game_state);


int main() {
    // This is to ensure that the output is displayed correctly. It is not required for the assignment.
    // https://intellij-support.jetbrains.com/hc/en-us/community/posts/115000763330-Debugger-not-working-on-Windows-CLion-
    setbuf(stdout, 0);

    // start by setting the console name to "Hello World"
    printf("\x1b]0; NotFlappyBird \x07");
    wait_for_user_to_resize_console();
    printf("Time in millis: %llu\n", millis());

    // hide the cursor
    printf(CSI "?25l");

    struct DisplayState display_state = {};
    struct GameState game_state = {};
    game_state.entity_count = 0;
    game_state.player_x = SCREEN_WIDTH / 2;
    game_state.player_y = SCREEN_HEIGHT / 2;
    game_state.screen_type = TITLE_SCREEN;

    game_state.title_text = create_entity(OVERLAY);
    game_state.title_text.x = SCREEN_WIDTH / 2;
    game_state.title_text.y = SCREEN_HEIGHT / 2;
    add_entity_view_from_file(&game_state.title_text, "not_flappy_bird.entity");
    register_entity(&game_state, &game_state.title_text);

    game_state.start_button = create_entity(OVERLAY);
    add_entity_view_from_file(&game_state.start_button, "press_space_to_start.entity");
    game_state.start_button.x = 0 - game_state.start_button.views[0].width;
    game_state.start_button.y = SCREEN_HEIGHT / 2 + 30;
    register_entity(&game_state, &game_state.start_button);

    create_obstacle(&game_state, game_state.player_x, game_state.player_y, 8);
    create_obstacle(&game_state, game_state.player_x + 90, game_state.player_y - 10, 9);
    create_obstacle(&game_state, game_state.player_x - 90, game_state.player_y + 10, 10);

    game_state.bird = (struct Bird) {create_entity(BIRD), 0};
    add_entity_view_from_file(&game_state.bird.entity, "bird_0.entity");
    add_entity_view_from_file(&game_state.bird.entity, "bird_1.entity");
    add_entity_view_from_file(&game_state.bird.entity, "bird_2.entity");
    register_entity(&game_state, &game_state.bird.entity);

    game_state.score = 0;
    create_score_counter(&game_state, SCREEN_WIDTH - 9, 1);


    // set up periodic timers
    struct PeriodicTimer periodic_timers[] = {
            // periodic timer for world scrolling
            {50,  0, scroll_world},

            // periodic timer for bird flapping animation
            {250, 0, animate_bird},

            // periodic timer for keyboard input
            {20,  0, game_tick}
    };

    //exit(0);
    cls();
    bool quit = false;
    while (!quit) {
        if (millis() - display_state.last_frame_time > 1000 / FRAME_RATE) {
            render_next_frame(&display_state, &game_state);
            update_display(&display_state);
        }

        // Run periodic timers (better to be done without a function here but doing so in order to hit assignment criteria)
        // Use of function(s), with array of struct in parameter list:
        run_periodic_timers(&game_state, periodic_timers, sizeof(periodic_timers) / sizeof(struct PeriodicTimer));
    }

    return 0;
}

void get_viewport_size(int *rows, int *columns) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    *columns = csbi.dwMaximumWindowSize.X;
    *rows = csbi.dwMaximumWindowSize.Y;
}

/**
 * Clear the screen. Sets the viewport position to 0,0
 */
void cls() {
    // NOT MY CODE, SOURCES:
    // https://stackoverflow.com/questions/34842526/update-console-without-flickering-c
    // https://learn.microsoft.com/en-us/windows/console/clearing-the-screen?redirectedfrom=MSDN

    // Get the Win32 handle representing standard output.
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    COORD topLeft = {0, 0};

    fflush(stdout);

    // Figure out the current width and height of the console window
    if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
        // TODO: Handle failure!
        abort();
    }
    DWORD length = csbi.dwSize.X * csbi.dwSize.Y;

    DWORD written;

    // Flood-fill the console with spaces to clear it
    FillConsoleOutputCharacter(hOut, TEXT(' '), length, topLeft, &written);

    // Reset the attributes of every character to the default.
    // This clears all background colour formatting, if any.
    FillConsoleOutputAttribute(hOut, csbi.wAttributes, length, topLeft, &written);

    // Move the cursor back to the top left for the next sequence of writes
    SetConsoleCursorPosition(hOut, topLeft);
}

void wait_for_user_to_resize_console() {
    int screen_rows, screen_columns;
    // wait until the user sizes the console appropriately. Screen size should be 100 columns by 40 rows minimum
    while (screen_columns < SCREEN_WIDTH || screen_rows < SCREEN_HEIGHT) {
        get_viewport_size(&screen_rows, &screen_columns);
        printf("Please resize the console to at least %d columns by %d rows "
               "(current size: %d columns by %d rows)", SCREEN_WIDTH, SCREEN_HEIGHT, screen_columns, screen_rows);
        usleep(1000000 / 10);
        cls();
    }
}

void set_cursor(int x, int y) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    COORD Position = {x, y};
    SetConsoleCursorPosition(hOut, Position);
}

/**
 * @return UNIX time in milliseconds
 */
long long millis() {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (((long long) tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

/**
 * Function to render a frame to the `next_frame` buffer in the DisplayState struct. Called whenever it is determined
 * that a new frame should be rendered.
 * @param display_state
 * @param game_state
 */
void render_next_frame(struct DisplayState *display_state, struct GameState *game_state) {
    // clear next frame
    memset(display_state->next_frame, ' ', sizeof(display_state->next_frame));

    // loop through registered entities and draw them
    for (int i = 0; i < game_state->entity_count; i++) {
        struct Entity *entity = game_state->entities[i];
        render_entity(entity, display_state->next_frame);
    }

    // draw an X in the current player position
    //display_state->next_frame[game_state->player_x][game_state->player_y] = 'X';

    // draw a border around the screen (extreme values of x and y)
    for (int x = 0; x < SCREEN_WIDTH; x++) {
        display_state->next_frame[x][0] = '=';
        display_state->next_frame[x][SCREEN_HEIGHT - 1] = '=';
    }
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        display_state->next_frame[0][y] = '|';
        display_state->next_frame[SCREEN_WIDTH - 1][y] = '|';
    }
}

/**
 * Updates the display with the next frame if the time since the last frame is greater than the frame period
 * @param display_state The display state
 */
void update_display(struct DisplayState *display_state) {
    // update the pixels on the screen that are different to the current frame
    // by doing this we only update the pixels that need to be updated
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            if (display_state->current_frame[x][y] != display_state->next_frame[x][y]) {
                set_cursor(x, y);
                printf("%c", display_state->next_frame[x][y]);
                display_state->current_frame[x][y] = display_state->next_frame[x][y];
            }
        }
    }
    display_state->last_frame_time = millis();
}

/**
 * Renders an entity to the specified frame buffer
 * @param entity The entity to render
 * @param frame The frame buffer to render to
 */
void render_entity(struct Entity *entity, char frame[SCREEN_WIDTH][SCREEN_HEIGHT]) {
    if (!entity->visible) {
        return;
    }

    // get the currently selected EntityView
    struct EntityView *view = entity->views + entity->current_view;

    // calculate start position of the entity (top left corner)
    int start_x = entity->x - view->origin_x;
    int start_y = entity->y - view->origin_y;

    int current_x = start_x;
    int current_y = start_y;

    // loop through all the chars in the view display
    for (int i = 0; i < view->display_size - 1; i++) {
        // check for newline or carriage return, in which case we move to the next line
        if (view->display[i] == '\n' || view->display[i] == '\r') {
            current_x = start_x;
            current_y++;
            continue;
        }

        // print the char to the screen if it fits within the screen bounds
        if (current_x >= 0 && current_x < SCREEN_WIDTH && current_y >= 0 && current_y < SCREEN_HEIGHT) {
            frame[current_x][current_y] = view->display[i];
        }
        current_x++;
    }
}

struct Entity create_entity(enum EntityType type) {
    struct Entity result = {};
    result.type = type;
    result.visible = true;
    return result;
}

void add_entity_view_from_file(struct Entity *entity, char *filename) {
    // create an empty EntityView
    struct EntityView view = {};
    view.display = NULL;

    // open the file
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error opening file '%s'", filename);
        exit(1);
    }

    // parse the file
    int num = fscanf(file, "width %d\nheight %d\norigin_x %d\norigin_y %d\n",
                     &view.width, &view.height, &view.origin_x, &view.origin_y);

    // check that the file was parsed correctly
    if (num != 4) {
        printf("Error parsing entity file '%s'\n", filename);
        exit(1);
    }

    char c = EOF;

    // seek back in the file until we hit a newline / carriage return
    // this is because whitespace gets deleted by `fscanf`
    while (c != '\n' && c != '\r') {
        fseek(file, -2, SEEK_CUR);
        c = fgetc(file);
    }

    while (c) {
        // read one character from the file
        c = fgetc(file);
        size_t index = 0;
        char *tmp = NULL;

        // check if we need to stop (we reached the end of the file)
        if (c == EOF) {
            c = 0;
        }

        view.display_size += 1;
        tmp = realloc(view.display, view.display_size);
        if (tmp == NULL) {
            printf("Error allocating memory for entity view display\n");
            exit(1);
        }
        view.display = tmp;

        view.display[view.display_size - 1] = c;
    }

    // close the file
    fclose(file);

    // add the new view to the Entity
    entity->views[entity->num_views] = view;

    // increment the number of views
    entity->num_views++;

    printf("Loaded entity view: %s\n", filename);
}

void next_entity_view(struct Entity *entity) {
    entity->current_view = (entity->current_view + 1) % entity->num_views;
}

void register_entity(struct GameState *game_state, struct Entity *entity) {
    printf("Registering entity:\n");
    // print all views

    for (int i = 0; i < entity->num_views; i++) {
        printf("view %d:\n", i);
        printf("width: %d\n", entity->views[i].width);
        printf("height: %d\n", entity->views[i].height);
        printf("origin_x: %d\n", entity->views[i].origin_x);
        printf("origin_y: %d\n", entity->views[i].origin_y);
        printf("display:\n%s\n", entity->views[i].display);
    }
    game_state->entity_count += 1;
    game_state->entities[game_state->entity_count - 1] = entity;
}

void create_obstacle(struct GameState *game_state, int x, int y, int gap_size) {

    struct Obstacle *output = &game_state->obstacles[game_state->obstacle_count];

    struct Entity obstacle_top = create_entity(OBSTACLE);
    add_entity_view_from_file(&obstacle_top, "obstacle_top.entity");

    struct Entity obstacle_bottom = create_entity(OBSTACLE);
    add_entity_view_from_file(&obstacle_bottom, "obstacle_bottom.entity");

    *output = (struct Obstacle) {
            .x = x,
            .y = y,
            .gap_size = gap_size,
            .top_entity = obstacle_top,
            .bottom_entity = obstacle_bottom,
            .score_collected = false
    };

    game_state->obstacle_count++;

    register_entity(game_state, &output->top_entity);
    register_entity(game_state, &output->bottom_entity);

    update_obstacle(output, game_state);
}

void update_obstacle(struct Obstacle *obstacle, struct GameState *game_state) {
    // set positions of the entities
    obstacle->top_entity.x = obstacle->x;
    obstacle->bottom_entity.x = obstacle->x;
    obstacle->top_entity.y = obstacle->y - obstacle->gap_size;
    obstacle->bottom_entity.y = obstacle->y + obstacle->gap_size;
}

void scroll_world(struct GameState *game_state) {
    // loop through obstacles
    for (int i = 0; i < game_state->obstacle_count; i++) {
        game_state->obstacles[i].x -= 1;

        if (game_state->screen_type == GAME_SCREEN) {
            // check if the player scored a point from this obstacle
            if (game_state->obstacles[i].x < game_state->bird.entity.x && !game_state->obstacles[i].score_collected) {
                game_state->score += 1;
                update_score_counter(game_state);
                printf("\x1b]0; Score: %d \x07", game_state->score);
                game_state->obstacles[i].score_collected = true;
            }
        }

        if (game_state->obstacles[i].x < -game_state->obstacles[i].top_entity.views[0].width) {
            game_state->obstacles[i].x = SCREEN_WIDTH;
            game_state->obstacles[i].y = (rand() % (SCREEN_HEIGHT - SCREEN_HEIGHT / 2)) + SCREEN_HEIGHT / 4;
            game_state->obstacles[i].score_collected = false;
        }
        update_obstacle(&game_state->obstacles[i], game_state);
    }

    if (game_state->screen_type == TITLE_SCREEN) {
        // scroll the "start" text
        game_state->start_button.x++;
        if (game_state->start_button.x > SCREEN_WIDTH) {
            game_state->start_button.x = 0 - game_state->start_button.views[0].width;
        }
    }
}

void animate_bird(struct GameState *game_state) {
    next_entity_view(&game_state->bird.entity);
}

void game_tick(struct GameState *game_state) {
    for (int i = 0; i < 256; i++) {
        if (GetAsyncKeyState(i) & 0x8000) { // Check if key is pressed
            if (i == VK_LEFT) { // left arrow
                game_state->bird.entity.x--;
            }
            if (i == VK_RIGHT) { // right arrow
                game_state->bird.entity.x++;
            }
            if (i == VK_SPACE) { // space bar
                if (game_state->screen_type == TITLE_SCREEN) {
                    start_game(game_state);
                }
                if (game_state->bird.velocity > -2) {
                    game_state->bird.velocity -= 2;
                }
                next_entity_view(&game_state->bird.entity);
            }
        }
    }

    // apply gravity
    if (game_state->bird.velocity < 1) {
        game_state->bird.velocity += 0.2;
    }

    if (game_state->screen_type == TITLE_SCREEN) {
        // auto fly the bird to stay in the bottom half of the screen
        if (game_state->bird.entity.y > SCREEN_HEIGHT - SCREEN_HEIGHT / 4) {
            game_state->bird.velocity -= 3 + (float) (rand() % 10) / 8;
        }

        // move the bird to the right
        if (game_state->bird.entity.x < SCREEN_WIDTH) {
            game_state->bird.entity.x++;
        } else {  // teleport the bird back to the origin
            game_state->bird.entity.x = 0;
            game_state->bird.entity.y = 0;
        }
    }

    if (game_state->screen_type == GAME_SCREEN) {
        // check for collision with between bird and obstacles using check_collision()
        for (int i = 0; i < game_state->obstacle_count; i++) {
            if (check_collision(&game_state->bird.entity, &game_state->obstacles[i].top_entity) ||
                check_collision(&game_state->bird.entity, &game_state->obstacles[i].bottom_entity)) {
                end_game(game_state);
            }
        }

        // check for collision with the edges of the screen
        if (game_state->bird.entity.x < 0 || game_state->bird.entity.x > SCREEN_WIDTH ||
            game_state->bird.entity.y < 0 || game_state->bird.entity.y > SCREEN_HEIGHT) {
            end_game(game_state);
        }
    }

    game_state->bird.entity.y += (int) game_state->bird.velocity;
}

void
run_periodic_timers(struct GameState *game_state, struct PeriodicTimer periodic_timers[], size_t periodic_timer_count) {
    for (int i = 0; i < periodic_timer_count; i++) {
        if (millis() - periodic_timers[i].last_trigger_time > periodic_timers[i].period) {
            periodic_timers[i].callback(game_state);
            periodic_timers[i].last_trigger_time = millis();
        }
    }
}

void start_game(struct GameState *game_state) {
    game_state->screen_type = GAME_SCREEN;
    game_state->start_button.visible = false;
    game_state->title_text.visible = false;
    game_state->bird.entity.x = 10;
    game_state->bird.entity.y = SCREEN_HEIGHT/2;
    game_state->score = 0;
    update_score_counter(game_state);

    // set positions of obstacles
    for (int i = 0; i < game_state->obstacle_count; i++) {
        game_state->obstacles[i].x = SCREEN_WIDTH / 4 + (i * SCREEN_WIDTH / game_state->obstacle_count);
        game_state->obstacles[i].y = (rand() % (SCREEN_HEIGHT - SCREEN_HEIGHT / 2)) + SCREEN_HEIGHT / 4;
        game_state->obstacles[i].score_collected = false;
        update_obstacle(&game_state->obstacles[i], game_state);
    }
}

bool check_collision(struct Entity *entity1, struct Entity *entity2) {
    struct EntityView view1 = entity1->views[entity1->current_view];
    struct EntityView view2 = entity2->views[entity2->current_view];

    int x1 = entity1->x - view1.origin_x;
    int y1 = entity1->y - view1.origin_y;
    int x2 = entity2->x - view2.origin_x;
    int y2 = entity2->y - view2.origin_y;

    int w1 = view1.width;
    int h1 = view1.height;

    int w2 = view2.width;
    int h2 = view2.height;

    bool collision_x = ((x1 < x2 && x1 + w1 > x2) || (x2 < x1 && x2 + w2 > x1));
    bool collision_y = ((y1 < y2 && y1 + h1 > y2) || (y2 < y1 && y2 + h2 > y1));

    return (collision_x && collision_y);
}

void end_game(struct GameState *game_state) {
    game_state->screen_type = TITLE_SCREEN;
    game_state->score = 0;
    game_state->title_text.visible = true;
    game_state->start_button.visible = true;
}

void create_score_counter(struct GameState *game_state, int x, int y) {
    // load entity views
    for (int digit_number = 0; digit_number < SCORE_COUNTER_DIGITS; digit_number++) {
        struct Entity *digit = &game_state->score_counter.digits[digit_number];
        for (int i = 0; i < 10; i++) {
            char filename[10];
            sprintf(filename, "%d.entity", i);
            add_entity_view_from_file(digit, filename);
        }
        digit->visible = true;
        digit->x = x - (digit_number * (digit->views[0].width + 1));
        digit->y = y;
        register_entity(game_state, digit);
    }

    update_score_counter(game_state);
}

void update_score_counter(struct GameState *game_state) {
    struct ScoreCounter *score_counter = &game_state->score_counter;
    int score = game_state->score;

    int digit_number = 0;
    while (score > 0) {
        int digit = score % 10;
        score_counter->digits[digit_number].current_view = digit;
        score_counter->digits[digit_number].visible = true;
        score /= 10;
        digit_number++;
    }
    for (int i = digit_number; i < SCORE_COUNTER_DIGITS; i++) {
        score_counter->digits[i].visible = false;
    }
}
