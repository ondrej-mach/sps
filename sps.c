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
#define MAX_CELL_LENGTH 1001
#define MAX_COMMAND_LENGTH 1001
#define MAX_FILENAME_LENGTH 1001
#define MAX_DELIMITERS 1001

// struct for each cell
// might not be necessary, but makes the program more extensible
typedef struct {
    char *str;
} Cell;

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
typedef struct {
    unsigned rows, cols;
    Cell **cells;
    Selection selection;
    char delim;
} Table;

// all program states
// these are returned by most functions that can fail in any way
typedef enum {
    SUCCESS = 0,
    NOT_FOUND,
    ERR_GENERIC,
    ERR_TOO_LONG,
    ERR_OUT_OF_RANGE,
    ERR_BAD_SYNTAX,
    ERR_BAD_FORMAT,
    ERR_TABLE_EMPTY,
    ERR_FILE_ACCESS,
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

// ---------- CELL FUNCTIONS -----------

// constructs a new empty cell
void cell_ctor(Cell *cell) {
    // only the termination character
    cell->str = calloc(1, sizeof(char));
}

// destructs a cell
void cell_dtor(Cell *cell) {
    free(cell->str);
    cell->str = NULL;
}

// writes chars from buffer into a cell
State writeCell(Cell *cell, char *src) {
    char *p = realloc(cell->str, strlen(src));
    if (p)
        cell->str = p;
    else
        return ERR_MEMORY;

    strcpy(cell->str, src);
    return SUCCESS;
}

// prints contents of one cell into a file
void printCell(Cell *cell, FILE *f) {
    // TODO special characters
    fprintf(f, "%s", cell->str);
}

// ---------- STRING FUNCTIONS ------------

// gets rid of all the escape characters
// quotation marks etc.
// returns how many characters in original string were parsed
size_t parseString(char *dst, char *src, char *delims) {
    int srcIndex = 0, dstIndex = 0;
    bool isQuoted = false, isEscaped = false;

    while (src[srcIndex] != '\0') {
        // if escape character
        if ((src[srcIndex] == '\\') && !isEscaped) {
            isEscaped = true;
            srcIndex++;
            continue;
        }

        // only first character can start quoted cell
        if ((src[srcIndex] == '\"') && (srcIndex == 0) && !isEscaped) {
            isQuoted = !isQuoted;
            srcIndex++;
            continue;
        }

        // if scanned character is delimiter
        if (strchr(delims, src[srcIndex]) && !isEscaped && !isQuoted)
            break;

        // check for \n is after check for escape character
        // it is the only character, that cannot be escaped
        if (src[srcIndex] == '\n')  {
            // it is illegal to escape or quote '\n'
            if (isEscaped || isQuoted)
                return 0;
            break;
        }

        // if there is nothing special about the characters
        // write it into the buffer
        dst[dstIndex] = src[srcIndex];
        isEscaped = false;
        srcIndex++;
        dstIndex++;
    }
    dst[dstIndex] = '\0';
    return srcIndex;
}

char *fileToBuffer(FILE *f) {
    // get the file into a buffer
    char *buffer = malloc(sizeof(char));;
    int i = 0;
    char c;

    while ((buffer[i] = fgetc(f)) != EOF) {
        i++;
        buffer = realloc(buffer, (i+1) * sizeof(char));
    }
    buffer[i] = '\0';
}

// ---------- SIMPLE TABLE FUNCTIONS -----------

// constructs a new empty table
void table_ctor(Table *table) {
    table->rows = 0;
    table->cols = 0;
    table->cells = NULL;
}

// deallocates all the pointers in the table structure
void table_dtor(Table *table) {
    for (unsigned i=0; i < table->rows; i++) {
        for (unsigned j=0; j < table->cols; j++) {
            cell_dtor(&table->cells[i][j]);
        }
        free(table->cells[i]);
    }

    free(table->cells);
    table->cells = NULL;

    table->rows = 0;
    table->cols = 0;
}

// adds an empty row to the end of the table
State addRow(Table *table) {
    // allocate one more row pointer in the array
    Cell **p = realloc(table->cells, (table->rows + 1) * sizeof(Cell *));
    if (p)
        table->cells = p;
    else
        return ERR_MEMORY;

    // allocate new cell array for the new row
    table->cells[table->rows] = malloc(table->cols * sizeof(Cell));
    if (table->cells[table->rows] == NULL)
        return ERR_MEMORY;

    // initialize all the cells in the new row
    for (unsigned i=0; i < table->cols; i++) {
        cell_ctor(&table->cells[table->rows][i]);
    }
    table->rows++;
    return SUCCESS;
}

// deletes the last row from the table
void deleteRow(Table *table) {
    // go through all the cells in the last row
    // and destruct them
    for (unsigned i=0; i < table->cols; i++) {
        cell_dtor(&table->cells[table->rows - 1][i]);
    }
    table->rows--;
}

// adds a column to the end of the table
State addCol(Table *table) {
    for (unsigned i=0; i < table->rows; i++) {
        table->cells[i] = realloc(table->cells[i], (table->cols + 1) * sizeof(Cell));
        cell_ctor(&table->cells[i][table->cols]);
    }
    table->cols++;
    return SUCCESS;
}

// deletes the last column of the table
void deleteCol(Table *table) {
    for (unsigned i=0; i < table->rows; i++) {
        cell_dtor(&table->cells[i][table->cols - 1]);
    }
    table->cols--;
}

// ---------- MORE COMPLEX FUNCTIONS -----------

// Reads table from stdin and saves it into the table structure
// The function also reads delimiters from arguments
// Returns program state
// Expects an empty table
State readTable(Table *table, FILE *f, char *delimiters) {
    // default delimiters
    char *defaultDelimiters = " ";
    if (delimiters == NULL)
        delimiters = defaultDelimiters;
    // set the table's main delimiter
    table->delim = delimiters[0];

    fileBuffer = fileToBuffer(f);

    // current row and column
    unsigned row=0, col=0;
    // points at current character

    size_t i = 0;
    while (true) {
        char cellBuffer[MAX_CELL_LENGTH];
        size_t shift = parseString(cellBuffer, &fileBuffer[i], delimiters);
        // +1 to skip the delimiter
        i += shift + 1;

        // write to table
        if (col >= table->cols)
            addCol(table);

        if (row >= table->rows)
            addRow(table);

        writeCell(&table->cells[row][col], cellBuffer);

        if (fileBuffer[i-1] == '\0') {
            break;

        if (fileBuffer[i-1] == '\n') {
            // end of line
            row++;
            col = 0;
            continue;
        }

        if (strchr(delimiters, fileBuffer[i-1]))
            // delimiter
            col++;
            continue;
        }
        // if nothing matches, the scanned STR was bad
        return ERR_BAD_FORMAT;
    }
    free(fileBuffer);
    return SUCCESS;
}

// prints the table into a file
void printTable(Table *table, FILE *f) {
    for (unsigned i=0; i < table->rows; i++) {
        for (unsigned j=0; j < table->cols; j++) {
            printCell(&table->cells[i][j], f);

            if (j < table->cols - 1)
                fputc(table->delim, f);
            else
                fputc('\n', f);
        }
    }
}


// reads delimiterrrs from arguments
State parseArguments(int argc, char **argv, char *delimiters, char *commands, char *filename) {
    // in case nothing is found
    strcpy(delimiters, " ");

    int i = 1;

    // reading delimiters
    if (strcmp("-d", argv[i]) == 0) {
        if (++i >= argc)
            return ERR_BAD_SYNTAX;

        strcpy(delimiters, argv[i]);
        if (++i >= argc)
            return ERR_BAD_SYNTAX;
    }

    // reading commands from file
    if (strcmp("-c", argv[i]) == 0) {
        if (++i >= argc)
            return ERR_BAD_SYNTAX;

        // file with commands
        FILE *fp = fopen(argv[i], "r");
        if (!fp)
            return ERR_FILE_ACCESS;

        fscanf(fp, "%1000s", commands);
        fclose(fp);

        if (++i >= argc)
            return ERR_BAD_SYNTAX;
    } else {
        // reading commands from the argument
        strcpy(commands, argv[i]);

        if (++i >= argc)
            return ERR_BAD_SYNTAX;
    }

    strcpy(filename, argv[i]);

    // if there are any arguments left at this point,
    // the syntax must be wrong
    if (i < argc-1)
        return ERR_BAD_SYNTAX;

    return SUCCESS;
}


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
        case SUCCESS:
            break;

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
    // the only instance of the table
    Table table;
    table_ctor(&table);

    State s = SUCCESS;
    // file with table data
    FILE *fp = NULL;

    char delimiters[MAX_DELIMITERS] = ";:";
    char commands[MAX_COMMAND_LENGTH];
    char filename[MAX_FILENAME_LENGTH];

/*
    if (s == SUCCESS)
        s = parseArguments(argc, argv, delimiters, commands, filename);

    if (s == SUCCESS) {
        fp = fopen(filename, "r+");
        if (!fp)
            s = ERR_FILE_ACCESS;
    }
*/
    if (s == SUCCESS)
        s = readTable(&table, stdin, delimiters);

    if (s == SUCCESS)
        printTable(&table, stdout);

    // if file is opened, close it
    if (fp)
        fclose(fp);

    table_dtor(&table);
    printErrorMessage(s);
    return s;
}
