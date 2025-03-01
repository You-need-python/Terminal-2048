#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>

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

struct config
{
    int score;
    int cols;
    int rows;
};
struct config E;

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

void getWindowSize(int *rows, int *cols)
{
    struct winsize ws;

    // to get the size of the terminal by <ESC> sequences
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
    {
        // to get how much to move the cursor right or down by
        *cols = 80;
        *rows = 80;
    }
    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    }
}

/*** Game ***/

Pos generateRandomPos()
{
    Pos pos = {rand() % SIZE, rand() % SIZE};
    return pos;
}

void initalize(int **arr)
{
    srand(time(NULL));

    E.score = 0;

    for (int i = 0; i < SIZE; i++)
    {
        free(arr[i]);
        arr[i] = malloc(SIZE * sizeof(int));
        memset(arr[i], 0, SIZE * sizeof(int));
    }

    Pos PosA = generateRandomPos();
    Pos PosB = generateRandomPos();

    arr[PosA.y][PosA.x] = 2;
    arr[PosB.y][PosB.x] = 2;
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
            E.score += arr[j - 1];
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

// returns 1 if arr is full and no two adjacent elements are equal.
int checkGameOver(int **arr)
{
    for (int i = 0; i < SIZE; i++)
    {
        if (searchArray(&arr[i][0], 0, SIZE))
            return 0;
        for (int j = 0; j < SIZE; j++)
            if ((j < SIZE - 1 && arr[i][j] == arr[i][j + 1]) || (i < SIZE - 1 && arr[i][j] == arr[i + 1][j]))
            {
                return 0;
            }
    }
    return 1;
}

void gameOver()
{
    write(STDOUT_FILENO, "GAMEOVER\n\r", 11); // TODO
    exit(0);
}

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
    int cellWidth = 5;
    int totalGridWidth = SIZE * cellWidth;
    int margin = (E.cols - totalGridWidth) / 2;

    if (margin < 0)
    {
        abAppend(ab, "Terminal too small!\n\r", 19);
        return;
    }

    for (int i = 0; i < SIZE; i++)
    {
        for (int p = 0; p < margin; p++)
            abAppend(ab, " ", 1);

        for (int j = 0; j < SIZE; j++)
        {
            char buf[12];
            int num = arr[i][j];
            int len = snprintf(buf, sizeof(buf), "%d", num);

            int leftPad = (cellWidth - len) / 2;
            int rightPad = cellWidth - len - leftPad;

            for (int p = 0; p < leftPad; p++)
                abAppend(ab, " ", 1);

            if (num)
                abAppend(ab, buf, len);
            else
                abAppend(ab, ".", 1);

            for (int p = 0; p < rightPad; p++)
                abAppend(ab, " ", 1);
        }
        abAppend(ab, "\n\r", 2);
    }
}

void editorDrawStatusBar(struct abuf *ab)
{
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status),
                       "PRESS CTRL-C TO QUIT | PRESS CTRL-R TO RESTART");
    int scoreLen = snprintf(rstatus, sizeof(rstatus), " SCORE: %d ", E.score);
    if (len > E.cols)
        len = E.cols;
    abAppend(ab, status, len);
    int pad = (E.cols - scoreLen) / 2;
    while (scoreLen < E.cols)
    {
        if (pad == len)
        {
            abAppend(ab, "\x1b[7m", 4);
            abAppend(ab, rstatus, scoreLen);
            abAppend(ab, "\x1b[m", 3);
            break;
        }
        else
        {
            abAppend(ab, " ", 1);
            len++;
        }
    }

    abAppend(ab, "\r\n", 2);
}

void editorRefreshScreen(int **arr)
{
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?1049h", 8);
    abAppend(&ab, "\x1b[H", 3);

    DrawGame(arr, &ab);
    abAppend(&ab, "\r\n", 2);
    editorDrawStatusBar(&ab);

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

    case CTRL_KEY('r'):
        initalize(arr);
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
        if (checkGameOver(arr))
            gameOver();
        break;

    default:
        break;
    }
}

int main()
{
    enableRawMode();

    getWindowSize(&E.rows, &E.cols);

    int **arr = malloc(sizeof(int *) * SIZE);
    for (int i = 0; i < SIZE; i++)
        arr[i] = malloc(sizeof(int) * SIZE);
    initalize(arr);

    write(STDOUT_FILENO, "\x1b[2J", 4);

    while (1)
    {
        editorRefreshScreen(arr);
        editorProcessKeypress(arr);
    }

    for (int i = 0; i < SIZE; i++)
        free(arr[i]);
    free(arr);
}