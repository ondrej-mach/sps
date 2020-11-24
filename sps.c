/**
 * Author: Ondrej Mach
 * VUT login: xmacho12
 * E-mail: ondrej.mach@seznam.cz
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define NUM_COMMANDS 25
#define MAX_CELL_LENGTH 1000

// struct for each cell
// character array isn't good enough, because there can be '\0'
// in the input
typedef struct {
    char *content;
    unsigned len;
} Cell;

Cell cell_ctor() {
    Cell cell;
    cell.content = NULL;
    cell.len = 0;
    return cell;
}

void cell_dtor(Cell *cell) {
    free(cell->content);
    cell->content = NULL;
    cell->len = 0;
}

void writeCell(Cell *cell, char *src, unsigned len) {
    cell->content = realloc(cell->content, len * sizeof(char));
    cell->len = len;
    memcpy(cell->content, src, len * sizeof(char));
}

void printCell(Cell *cell, FILE *f) {
    for (unsigned i=0; i<cell->len; i++) {
        fputc(cell->content[i], f);
    }
}

// selection is always a rectangle
typedef struct {
    unsigned startRow;
    unsigned startCol;
    unsigned endRow;
    unsigned endCol;
} Selection;

// struct for table
// stores only one main delimiter
// others get replaced while reading the table
// the content of the data array is basically CSV
typedef struct {
    unsigned rows, cols;
    Cell **cells;
    Selection selection;
    char delimiter;
} Table;


Table table_ctor() {
    Table table;
    table.rows = 0;
    table.cols = 0;
    table.delimiter = ' ';
    table.cells = NULL;
    // table->selection = TODO;
    return table;
}

void table_dtor(Table *table) {
    for (unsigned i=0; i < table->rows; i++) {
        for (unsigned j=0; j < table->rows; j++) {
            cell_dtor(&table->cells[i][j]);
        }
        free(table->cells[i]);
    }

    free(table->cells);
    table->cells = NULL;

    table->rows = 0;
    table->cols = 0;
}


// all program states
// these are returned by most functions that can fail in any way
typedef enum {
    SUCCESS = 0,
    NOT_FOUND,
    ERR_GENERIC,
    ERR_TOO_LONG,
    ERR_OUT_OF_RANGE,
    ERR_BAD_SYNTAX,
    ERR_TABLE_EMPTY,
    ERR_BAD_ORDER,
    ERR_BAD_TABLE,
    ERR_MEMORY
} State;


// categorizes every command
// program has to know which commands can be combined
typedef enum {
    NOT_SET = 0,
    DATA,
    LAYOUT,
    SELECTION,
} TypeOfCommand;


// always go together, easier to pass around
typedef struct {
    char name[16];
    int numParameters;
    bool hasStringParameter;
    TypeOfCommand type;
    // pointer for every function separately
    // what a perfect opportunity for union
    State (*fnZero)(Table*);
    State (*fnOne)(Table*, int);
    State (*fnTwo)(Table*, int, int);
    // function with one integer argument and one string
    State (*fnOneStr)(Table*, int, char*);
} Command;


State addRow(Table *table) {
    table->cells = realloc(table->cells, (table->rows + 1) * sizeof(Cell *));
    table->cells[table->rows] = malloc(table->cols * sizeof(Cell));

    for (unsigned i=0; i < table->cols; i++) {
        table->cells[table->rows][i] = cell_ctor();

        if (table->cells[table->rows][i].content == NULL) {
            // if the allocation fails
            return ERR_MEMORY;
        }
    }
    table->rows++;
    return SUCCESS;
}

void deleteRow(Table *table) {

    for (unsigned i=0; i < table->cols; i++) {
        cell_dtor(&table->cells[table->rows - 1][i]);
    }

    table->rows--;
}

State addCol(Table *table) {
    table->cols++;

    for (unsigned i=0; i < table->rows; i++) {
        table->cells[i] = realloc(table->cells[i], table->cols * sizeof(Cell *));
        table->cells[i][table->cols - 1] = cell_ctor();
    }

    return SUCCESS;
}

void deleteCol(Table *table) {
    for (unsigned i=0; i < table->rows; i++) {
        cell_dtor(&table->cells[i][table->cols - 1]);
    }

    table->cols--;
}


// Reads table from stdin and saves it into the table structure
// The function also reads delimiters from arguments
// Returns program state
// Expects an empty table
State readTable(Table *table, FILE *f, char *delimiters) {
    // set the table's main delimiter
    table->delimiter = delimiters[0];

    char c; // scanned character

    unsigned row=0, col=0, i=0; // current row and column
    char buffer[MAX_CELL_LENGTH];

    addRow(table);
    addCol(table);

    while ((c = fgetc(f)) != EOF) {
        // if scanned character is delimiter
        if (strchr(delimiters, c)) {
            if (col >= table->cols)
                addCol(table);

            writeCell(&table->cells[row][col], buffer, i);
            i = 0;
            col++;
            continue;
        }

        if (c == '\\') {
            c = fgetc(f);
        }

        // check for \n is after check for escape character
        // it is the only character, that cannot be escaped
        if (c == '\n')  {
            addRow(table);
            writeCell(&table->cells[row][col], buffer, i);
            i = 0;
            col = 0;
            row++;
        }
        buffer[i++] = c;
    }
    return SUCCESS;
}



State printTable(Table *table, FILE *f) {
    for (unsigned i=0; i < table->rows; i++) {
        for (unsigned j=0; j < table->cols; j++) {
            printCell(&table->cells[i][j], f);

            if (j < table->cols - 1)
                fputc(';', f);
            else
                fputc('\n', f);
        }
    }
    return SUCCESS;
}



/*
// inserts an empty row into the table
state_t irow(table_t *table, int row) {


    return SUCCESS;
}

// appends an empty row to the table
state_t arow(table_t *table) {
    return irow(table, countRows(table)+1);
}

// deletes a row from the table
state_t drow(table_t *table, int row) {
    if (row < 1 || row > countRows(table))
        return ERR_OUT_OF_RANGE;

    char *p = getCellPtr(row, 1, table);

    // count how many characters have to be shifted out
    int i = 0;
    while (p[i++] != '\n');

    shiftData(p, -i, table);

    return SUCCESS;
}

// deletes multiple rows from the table
state_t drows(table_t *table, int m, int n) {
    if (n < m)
        return ERR_BAD_SYNTAX;

    for (int i=m; i<=n; i++) {
        state_t s = drow(table, m);
        if (s != SUCCESS)
            return s;
    }

    return SUCCESS;
}

// inserts an empty column into the table
state_t icol(table_t *table, int col) {
    state_t state;

    if (col < 1 || col > countColumns(table)+1)
        return ERR_OUT_OF_RANGE;

    int numRows = countRows(table);

    // for each row
    for (int i=1; i<=numRows; i++) {
        char *p = getCellPtr(i, col, table);
        state = shiftData(p, 1, table);
        *p = table->delimiter;
    }

    return state;
}

// appends an empty column to the table
state_t acol(table_t *table) {
    return icol(table, countColumns(table)+1);
}

// deletes a column from the table
state_t dcol(table_t *table, int col) {
    state_t state;

    if (col < 1 || col > countColumns(table))
        return ERR_OUT_OF_RANGE;

    if (countColumns(table) == 1)
        return ERR_TABLE_EMPTY;

    int numRows = countRows(table);

    // for each row
    for (int i=1; i<=numRows; i++) {
        char *p = getCellPtr(i, col, table);

        int j=0;
        while (!endOfCell(p[j], table))
            j++;
        // pointer will be at delimiter or \n

        // if is is the last column, delimiter in front of the column will be deleted
        if (p[j] == table->delimiter)
            state = shiftData(p, -(j+1), table);
        else
            state = shiftData(&p[-1], -(j+1), table);


        if (state != SUCCESS)
            return state;
    }
    return state;
}


// deletes multiple columns from the table
state_t dcols(table_t *table, int m, int n) {
    if (n < m)
        return ERR_BAD_SYNTAX;

    for (int i=m; i<=n; i++) {
        state_t s = dcol(table, m);
        if (s != SUCCESS)
            return s;
    }

    return SUCCESS;
}

// Prints the table into stdout
void printTable(table_t *table) {
    printf("%s", table->data);
}

// tries to read int from current argument
// if it succeeds returns true and increments argument index
// if there is -, the function assigns DASH_NUMBER constant
bool readInt(arguments_t *args, int *n) {
    if (args->index >= args->argc)
        return false;

    // special case for -
    if (strcmp(args->argv[args->index], "-") == 0) {
        *n = DASH_NUMBER;
        args->index++;
        return true;
    }

    char *pEnd;
    *n = strtol(args->argv[args->index], &pEnd, 10);
    if (*pEnd != '\0')
        return false;

    args->index++;
    return true;
}

// takes a float and rounds it
int roundNumber(float num) {
    if (num < 0)
        return num - 0.5;

    return num + 0.5;
}

// takes in string
// if there is a number inside,
// the function rounds it and writes it back to the string
void roundLine(char *buffer) {
    char *pEnd;
    double num = strtod(buffer, &pEnd);

    if (*pEnd == '\0') {
        // if the input value is valid
        sprintf(buffer, "%d", roundNumber(num));
    }
}

// takes in string
// if there is a number in it,
// the function makes integer out of the number and writes it back to the string
void intLine(char *buffer) {
    char *pEnd;
    double num = strtod(buffer, &pEnd);

    if (*pEnd == '\0') {
        // if the reading worked fine
        // and there's only number in the cell
        sprintf(buffer, "%d", (int)num);
    }
}

// takes in string and makes it uppercase
void upperLine(char *buffer) {
    int i=0;
    do {
        if ((buffer[i]>='a') && (buffer[i]<='z'))
            buffer[i] -= 'a'-'A';
    } while (buffer[i++] != '\0');
}

// takes in string and makes it lowercase
void lowerLine(char *buffer) {
    int i=0;
    do {
        if ((buffer[i]>='A') && (buffer[i]<='Z'))
            buffer[i] += 'a'-'A';
    } while (buffer[i++] != '\0');
}

// modifies each cell of the column, but only if it lies in the chosen row
// The string in cell is modified with the modFunction
state_t modifyData(table_t *table, int col, void(*modFunction)(char *)) {
    int numRows = countRows(table);
    for (int row=1; row<=numRows; row++) {
        if (table->rowSelected[row]) {
            char buffer[MAX_CELL_LENGTH];
            state_t state;

            state = readCell(table, row, col, buffer);
            if (state != SUCCESS)
                return state;

            modFunction(buffer);

            writeCell(table, row, col, buffer);
        }
    }
    return SUCCESS;
}

// all of these functions use modifyData()
state_t upperColumn(table_t *table, int col) {
    return modifyData(table, col, &upperLine);
}

state_t lowerColumn(table_t *table, int col) {
    return modifyData(table, col, &lowerLine);
}

state_t roundColumn(table_t *table, int col) {
    return modifyData(table, col, &roundLine);
}

state_t intColumn(table_t *table, int col) {
    return modifyData(table, col, &intLine);
}

// functions to rewrite data in columns
state_t setColumn(table_t *table, int col, char *content) {
    int numRows = countRows(table);

    for (int row=1; row<=numRows; row++) {
        if (table->rowSelected[row]) {
            state_t state = writeCell(table, row, col, content);
            if (state != SUCCESS)
                return state;
        }
    }
    return SUCCESS;
}

state_t copyColumn(table_t *table, int srcCol, int destCol) {
    int numRows = countRows(table);

    for (int row=1; row<=numRows; row++) {
        if (table->rowSelected[row]) {
            char buffer[MAX_CELL_LENGTH];
            state_t state = readCell(table, row, srcCol, buffer);
            if (state != SUCCESS)
                return state;

            state = writeCell(table, row, destCol, buffer);
            if (state != SUCCESS)
                return state;
        }
    }
    return SUCCESS;
}

state_t swapColumn(table_t *table, int col1, int col2) {
    int numRows = countRows(table);

    for (int row=1; row<=numRows; row++) {
        if (table->rowSelected[row]) {
            char content1[MAX_CELL_LENGTH];
            char content2[MAX_CELL_LENGTH];
            state_t state;

            state = readCell(table, row, col1, content1);
            if (state != SUCCESS)
                return state;

            state = readCell(table, row, col2, content2);
            if (state != SUCCESS)
                return state;

            writeCell(table, row, col1, content2);
            writeCell(table, row, col2, content1);
        }
    }
    return SUCCESS;
}

state_t moveColumn(table_t *table, int n, int m) {
    int pos = n;
    int endPos = m;
    if (n < m)
        endPos--;

    // not really efficient, lower level implementation would be faster
    while (pos != endPos) {
        // direction is -1 or +1
        int direction = 2*(pos < endPos) - 1;

        state_t state = swapColumn(table, pos, pos + direction);
        if (state != SUCCESS)
            return state;

        pos += direction;
    }
    return SUCCESS;
}


state_t selectRows(table_t *table, int start, int end) {
    int numRows = countRows(table);
    int numCols = countColumns(table);

    if (end == DASH_NUMBER) {
        // command like "rows 5 -" selectslines from 5 to the end
        if (start == DASH_NUMBER) {
            // special case for "rows - -", which selects only the last line
            start = numCols;
        }
        end = numCols;
    }

    if (start > end)
        return ERR_BAD_SYNTAX;

    if ((end > numCols) || (start < 1))
        return ERR_OUT_OF_RANGE;

    for (int row=1; row<=numRows; row++) {
        bool selected = (row >= start) && (row <= end);
        table->rowSelected[row] = table->rowSelected[row] && selected;
    }

    return SUCCESS;
}

state_t selectBeginsWith(table_t *table, int col, char *str) {
    int numRows = countRows(table);

    if (col < 1 || col > countColumns(table))
        return ERR_OUT_OF_RANGE;

    for (int row=1; row<=numRows; row++) {
        char content[MAX_CELL_LENGTH];
        readCell(table, row, col, content);
        bool selected = (strstr(content, str) == &content[0]);
        table->rowSelected[row] = table->rowSelected[row] && selected;
    }
    return SUCCESS;
}

state_t selectContains(table_t *table, int col, char *str) {
    int numRows = countRows(table);

    if (col < 1 || col > countColumns(table))
        return ERR_OUT_OF_RANGE;

    for (int row=1; row<=numRows; row++) {
        char content[MAX_CELL_LENGTH];
        readCell(table, row, col, content);
        // if str is found in the content of the cell
        bool selected = (strstr(content, str) != NULL);
        table->rowSelected[row] = table->rowSelected[row] && selected;
    }
    return SUCCESS;
}

// select all rows of the table
// different form all the selection functions
// assigns the value directly, whereas the other functions use and operator
void selectAll(table_t *table) {
    int numRows = countRows(table);

    for (int row=1; row<=numRows; row++)
        table->rowSelected[row] = true;
}

// reads command's parameters from args and executes it
state_t executeCommand(command_t *command, arguments_t *args, table_t *table) {
    // read all command's parameters
    int parameters[command->numParameters];

    for (int k=0; k < command->numParameters; k++) {
        if (!readInt(args, &parameters[k])) {
            return ERR_BAD_SYNTAX;
        }
    }

    if (!command->hasStringParameter) {
        switch (command->numParameters) {
            case 0: return command->fnZero(table);
            case 1: return command->fnOne(table, parameters[0]);
            case 2: return command->fnTwo(table, parameters[0], parameters[1]);
        }
    }

    // if command does have string parameter
    // there is only one type of command with string
    char strParameter[MAX_CELL_LENGTH];
    strcpy(strParameter, args->argv[args->index]);
    args->index++;
    return command->fnOneStr(table, parameters[0], strParameter);
}

// Can these two commands be after each other
bool isValidOrder (type_of_command_t currentCommand, type_of_command_t lastCommand) {
    switch (lastCommand) {
        case NOT_SET:
            return true;
        case LAYOUT:
            // there can be only layout command
            if (currentCommand == LAYOUT)
                return true;
            break;

        case DATA:
            // there can be only data command
            return false;
            break;

        case SELECTION:
            // there can be another selection or data command
            if ((currentCommand == DATA) || (currentCommand == SELECTION))
                return true;
            break;

        default: // should never happen
            break;
    }
    return false;
}

// takes in arguments and recognizes commands
// commands are executed right after they are found
state_t parseCommands(arguments_t *args, table_t *table) {
    command_t commands[NUM_COMMANDS] = {
        {.type=LAYOUT, .name="irow", .numParameters=1, .fnOne=irow},
        {.type=LAYOUT, .name="arow", .numParameters=0, .fnZero=arow},
        {.type=LAYOUT, .name="drow", .numParameters=1, .fnOne=drow},
        {.type=LAYOUT, .name="drows", .numParameters=2, .fnTwo=drows},
        {.type=LAYOUT, .name="icol", .numParameters=1, .fnOne=icol},
        {.type=LAYOUT, .name="acol", .numParameters=0, .fnZero=acol},
        {.type=LAYOUT, .name="dcol", .numParameters=1, .fnOne=dcol},
        {.type=LAYOUT, .name="dcols", .numParameters=2, .fnTwo=dcols},

        {.type=DATA, .name="cset", .numParameters=1, .hasStringParameter=true, .fnOneStr=setColumn},
        {.type=DATA, .name="tolower", .numParameters=1, .fnOne=lowerColumn},
        {.type=DATA, .name="toupper", .numParameters=1, .fnOne=upperColumn},
        {.type=DATA, .name="round", .numParameters=1, .fnOne=roundColumn},
        {.type=DATA, .name="int", .numParameters=1, .fnOne=intColumn},
        {.type=DATA, .name="copy", .numParameters=2, .fnTwo=copyColumn},
        {.type=DATA, .name="swap", .numParameters=2, .fnTwo=swapColumn},
        {.type=DATA, .name="move", .numParameters=2, .fnTwo=moveColumn},

        {.type=SELECTION, .name="rows", .numParameters=2, .fnTwo=selectRows},
        {.type=SELECTION, .name="beginswith", .numParameters=1, .hasStringParameter=true, .fnOneStr=selectBeginsWith},
        {.type=SELECTION, .name="contains", .numParameters=1, .hasStringParameter=true, .fnOneStr=selectContains}
    };

    if (args->index >= args->argc)
        return NOT_FOUND;

    type_of_command_t lastCommandType = NOT_SET;
    while (args->index < args->argc) {
        // if no command is found, it is bad syntax
        state_t state = ERR_BAD_SYNTAX;

        // go through all the commands and check if any name matches
        for (int i=0; i<NUM_COMMANDS; i++) {
            if (strcmp(commands[i].name, args->argv[args->index]) == 0) {
                args->index++; //successfully found a valid command
                if (!isValidOrder(commands[i].type, lastCommandType))
                    return ERR_BAD_ORDER;

                state = executeCommand(&commands[i], args, table);
                lastCommandType = commands[i].type;
                // we don't have to check this argument anymore
                break;
            }
        }
        if (state != SUCCESS)
            return state;
    }
    return SUCCESS;
}

int main(int argc, char **argv) {
    arguments_t args = {.argc=argc, .index=1, .argv=argv};

    table_t table;
    state_t state;

    state = readTable(&args, &table);
    // by default all rows are selected
    selectAll(&table);

    if (state == SUCCESS)
        state = parseCommands(&args, &table);

    if (isEmpty(&table))
        state = ERR_TABLE_EMPTY;

    if (state == SUCCESS) {
        printTable(&table);
        return EXIT_SUCCESS;
    }

    printErrorMessage(state);
    return EXIT_FAILURE;
}*/

// prints basic help on how to use the program
void printUsage() {
    const char *usageString = "\nUsage:\n"
        "./sheet [-d DELIM] [Commands for editing the table]\n"
        "or\n"
        "./sheet [-d DELIM] [Row selection] [Command for processing the data]\n";

    fprintf(stderr, "%s", usageString);
}

// prints error message according to the error state
void printErrorMessage(State err_state) {
    switch(err_state) {
        case NOT_FOUND:
            fputs("No commands found\n", stderr);
            printUsage();
            break;

        case ERR_GENERIC:
            fputs("Generic error\n", stderr);
            break;

        case ERR_TOO_LONG:
            fputs("Maximum file size is 10kiB\n", stderr);
            break;

        case ERR_OUT_OF_RANGE:
            fputs("Given cell coordinates are out of range\n", stderr);
            break;

        case ERR_BAD_SYNTAX:
            fputs("Bad syntax\n", stderr);
            break;

        case ERR_TABLE_EMPTY:
            fputs("Table cannot be empty\n", stderr);
            break;

        case ERR_BAD_ORDER:
            fputs("Commands are used in wrong order\n", stderr);
            printUsage();
            break;

        case ERR_BAD_TABLE:
            fputs("Table has different numbers of columns in each row\n", stderr);
            break;

        default:
            fputs("Unknown error\n", stderr);
            break;
    }
}

int main(int argc, char **argv) {

    Table table = table_ctor();
    State state;
    char *delimiters = ":;";

    readTable(&table, stdin, delimiters);
    printTable(&table, stdout);
    table_dtor(&table);

    return EXIT_SUCCESS;
}
