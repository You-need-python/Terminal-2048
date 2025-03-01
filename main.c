#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

enum editorKey
{
    ARROW_LEFT = 1500,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
};

struct termios orig_termios;

typedef struct pos
{
    int y, x;
} Pos;

struct abuf
{
    char *b;
    int len;
};

#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define SIZE 4

void die(const char *s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void abFree(struct abuf *ab)
{
    free(ab->b);
}

void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len); // to allocate enough memory

    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
    {
        die("tcsetattr");
    }
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 10;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int searchArray(int *arr, int element, int size)
{
    for (int i = 0; i < size; i++)
    {
        if (arr[i] == element)
            return 1;
    }
    return 0;
}

void ReverseArray(int *arr, int size)
{
    for (int i = 0; i < size / 2; i++)
    {
        int temp = arr[i];
        arr[i] = arr[size - 1 - i];
        arr[size - 1 - i] = temp;
    }
}

/*** Game ***/

Pos generateRandomPos()
{
    Pos pos = {rand() % SIZE, rand() % SIZE};
    return pos;
}

void move(int *arr) //{2, 2, 4, 4} -> {4, 8, 0, 0} move to the direction of arr[0]
{
    int tmp = SIZE + 1;

    for (int i = 1; i < SIZE; i++)
    {
        if (!arr[i])
            continue;

        int j = i;
        while (j > 0 && !arr[j - 1])
            j--;

        arr[j] = arr[i];
        if (i != j)
            arr[i] = 0;

        if (j && arr[j - 1] == arr[j] && j - 1 != tmp)
        {
            arr[j - 1] = arr[j] * 2;
            arr[j] = 0;
            tmp = j - 1;
        }
    }
}

void addToRandomPos(int **arr)
{
    Pos pos = generateRandomPos();
    while (arr[pos.x][pos.y])
        pos = generateRandomPos();

    arr[pos.x][pos.y] = 2;
}

// //returns 1 if arr is full and no two adjacent elements are equal.
// int checkGameOver(int **arr)
// {
//     for (int i = 0; i < SIZE; i++)
//     {
//         if (searchArray(&arr[i][0], 0, SIZE)) return 0;
//         for (int j = 0; j < SIZE-1; j++)
//             if (arr[i][j] == arr[i][j]) return 0;
//     }
//     return 1;
// }

void setLine(int **arr, int index, int direction)
{
    int *buffer = malloc(sizeof(int) * SIZE);
    if (!buffer)
    {
        die("malloc failed");
    }

    if (direction == ARROW_UP || direction == ARROW_DOWN)
    {
        for (int j = 0; j < SIZE; j++)
            buffer[j] = arr[j][index];

        if (direction == ARROW_DOWN)
            ReverseArray(buffer, SIZE);
        move(buffer);
        if (direction == ARROW_DOWN)
            ReverseArray(buffer, SIZE);

        for (int j = 0; j < SIZE; j++)
            arr[j][index] = buffer[j];
    }
    else
    {
        for (int j = 0; j < SIZE; j++)
            buffer[j] = arr[index][j];

        if (direction == ARROW_RIGHT)
            ReverseArray(buffer, SIZE);
        move(buffer);
        if (direction == ARROW_RIGHT)
            ReverseArray(buffer, SIZE);

        for (int j = 0; j < SIZE; j++)
            arr[index][j] = buffer[j];
    }

    free(buffer);
}

/*** Screen ***/

void DrawGame(int **arr, struct abuf *ab)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        ws.ws_col = 80;
    }

    int cell_width = 5;
    int total_grid_width = SIZE * cell_width;
    int margin = (ws.ws_col - total_grid_width) / 2;

    if (margin < 0)
    {
        abAppend(ab, "Terminal too small!\n\r", 19);
        return;
    }

    for (int i = 0; i < SIZE; i++)
    {
        for (int p = 0; p < margin; p++)
        {
            abAppend(ab, " ", 1);
        }

        for (int j = 0; j < SIZE; j++)
        {
            char buf[12];
            int num = arr[i][j];
            int len = snprintf(buf, sizeof(buf), "%d", num);

            int left_pad = (cell_width - len) / 2;
            int right_pad = cell_width - len - left_pad;

            for (int p = 0; p < left_pad; p++)
            {
                abAppend(ab, " ", 1);
            }

            abAppend(ab, buf, len);

            for (int p = 0; p < right_pad; p++)
            {
                abAppend(ab, " ", 1);
            }
        }
        abAppend(ab, "\n\r", 2);
    }
}

void editorRefreshScreen(int **arr)
{
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?1049h", 8);
    abAppend(&ab, "\x1b[H", 3);

    DrawGame(arr, &ab);

    abAppend(&ab, "PRESS CTRL-C TO QUIT", 20);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    // if input is escape sequence
    if (c == '\x1b')
    {
        char seq[3]; // escape sequence: 27, '[', then one or two other characters

        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        // if the escape sequence is an arrow key escape sequence
        if (seq[0] == '[' && (seq[1] < '0' || seq[1] > '9'))
        {
            switch (seq[1])
            {
            case 'A':
                return ARROW_UP;
            case 'B':
                return ARROW_DOWN;
            case 'C':
                return ARROW_RIGHT;
            case 'D':
                return ARROW_LEFT;
            }
        }
        // otherwise
        return '\x1b';
    }
    else
    {
        return c;
    }
}

void editorProcessKeypress(int **arr)
{
    int c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('c'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        write(STDOUT_FILENO, "\x1b[?25h", 6);
        exit(0);
        break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_RIGHT:
    case ARROW_LEFT:
        for (int i = 0; i < SIZE; i++)
        {
            setLine(arr, i, c);
        }
        addToRandomPos(arr);
        break;
    default:
        break;
    }
}

int main()
{
    enableRawMode();

    /*** initalize ***/
    int **arr = malloc(sizeof(int *) * SIZE);
    for (int i = 0; i < SIZE; i++)
        arr[i] = malloc(sizeof(int) * SIZE);

    Pos PosA = generateRandomPos();
    Pos PosB = generateRandomPos();

    arr[PosA.y][PosA.x] = 2;
    arr[PosB.y][PosB.x] = 2;

    while (1)
    {
        editorRefreshScreen(arr);
        editorProcessKeypress(arr);
    }

    for (int i = 0; i < SIZE; i++)
    {
        free(arr[i]);
    }
    free(arr);
}