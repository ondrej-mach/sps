/**
 * Author: Ondrej Mach
 * VUT login: xmacho12
 * E-mail: ondrej.mach@seznam.cz
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

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

// struct for table
// stores only one main delimiter
// others get replaced while reading the table
typedef struct {
    // variables _0 to _9
    Cell cellVars[10];
    // Selection variable _
    Selection selVar;
} Variables;

// all program states
// these are returned by most functions that can fail in any way
typedef enum {
    SUCCESS = 0,
    NOT_FOUND,
    ERR_GENERIC,
    ERR_TOO_LONG,
    ERR_OUT_OF_RANGE,
    ERR_BAD_SYNTAX,
    ERR_BAD_COMMANDS,
    ERR_BAD_FORMAT,
    ERR_TABLE_EMPTY,
    ERR_FILE_ACCESS,
    ERR_BAD_ORDER,
    ERR_BAD_TABLE,
    ERR_MEMORY
} State;

// Everything, that commands might have access to
// is easier to extend, when a command needs something special
typedef struct {
    Table *table;
    // arguments ot the command itself
    char *argStr;
    Variables *vars;
    // be very careful, this influences the flow of the program
    unsigned *seqPtr;
} Context;

// struct for all the commands
typedef struct {
    // name is used only for parsing
    char name[16];
    State (*fn)(Context);
    // only used when executing
    char *argStr;
} Command;

// One structure is easier to manage than an array of commands
typedef struct {
    size_t len;
    // dynamic array with commands
    Command *cmds;
} Program;

typedef struct {
    char *delimiters;
    char *filename;
    char *commandString;
} Arguments;

// ---------- FUNCTION PROTOTYPES ------------

void printTable(Table *table, FILE *f);

// ---------- STRING FUNCTIONS ------------

// gets rid of all the escape characters
// quotation marks etc.
// returns how many characters in original string were parsed
size_t parseString(char *dst, char *src, char *delims) {
    int srcIndex = 0, dstIndex = 0;
    bool isQuoted = false, isEscaped = false;

    for (; src[srcIndex] != '\0'; srcIndex++) {
        // if escape character
        if ((src[srcIndex] == '\\') && !isEscaped) {
            isEscaped = true;
            continue;
        }
        // only first character can start quoted cell
        if ((src[srcIndex] == '\"') && (srcIndex == 0)) {
            isQuoted = true;
            continue;
        }
        // ending quotation
        if ((src[srcIndex] == '\"') && !isEscaped && isQuoted) {
            isQuoted = false;
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
        dstIndex++;
    }
    if (isQuoted)
        return 0;

    dst[dstIndex] = '\0';
    return srcIndex;
}

char *fileToBuffer(FILE *f) {
    // get the file into a buffer
    char *buffer = malloc(sizeof(char));;
    int i = 0;

    while ((buffer[i] = fgetc(f)) != EOF) {
        i++;
        buffer = realloc(buffer, (i+1) * sizeof(char));
    }
    buffer[i] = '\0';
    return buffer;
}

// checks if the first string begins with the second
int strbgn(const char *str, const char *substr) {
    size_t len = strlen(substr);
    return memcmp(str, substr, len);
}

// ---------- OTHER FUNCTIONS ------------

// initializes the selection to default values
void selection_init(Selection *sel) {
    sel->startRow = 1;
    sel->startCol = 1;
    sel->endRow = 1;
    sel->endCol = 1;
}

// ---------- CELL FUNCTIONS -----------

// constructs a new empty cell
State cell_ctor(Cell *cell) {
    // only the termination character
    cell->str = calloc(1, sizeof(char));
    if (cell->str == NULL)
        return ERR_MEMORY;
    return SUCCESS;
}

// destructs a cell
void cell_dtor(Cell *cell) {
    free(cell->str);
    cell->str = NULL;
}

// writes chars from buffer into a cell
State writeCell(Cell *cell, char *src) {
    free(cell->str);
    cell->str = malloc((strlen(src) + 1) * sizeof(char));
    if (cell->str == NULL)
        return ERR_MEMORY;

    strcpy(cell->str, src);
    return SUCCESS;
}

// writes chars from buffer into a cell
State deepCopyCell(Cell *dst, Cell *src) {
    return writeCell(dst, src->str);
}

// writes chars from buffer into a cell
State swapCell(Cell *c1, Cell *c2) {
    Cell tmp;
    State s = SUCCESS;

    s = cell_ctor(&tmp);

    if (s == SUCCESS)
        s = deepCopyCell(&tmp, c2);

    if (s == SUCCESS)
        s = deepCopyCell(c2, c1);

    if (s == SUCCESS)
        s = deepCopyCell(c1, &tmp);

    cell_dtor(&tmp);
    return SUCCESS;
}

// prints contents of one cell into a file
void printCell(Table *table, Cell *cell, FILE *f) {
    char buffer[2*strlen(cell->str) + 1];
    strcpy(buffer, "");
    // TODO
    if (strchr(cell->str, table->delim)) {
        // if there is delimiter in the cell
        strcat(buffer, "\"");
        strcat(buffer, cell->str);
        strcat(buffer, "\"");
    } else {
        strcat(buffer, cell->str);
    }
    fprintf(f, "%s", buffer);
}

// ---------- PROGRAM FUNCTIONS ------------

// initializes the program structure
State program_ctor(Program *prog) {
    prog->len = 0;
    prog->cmds = NULL;
    return SUCCESS;
}

// deallocates the program structure
void program_dtor(Program *prog) {
    for (size_t i=0; i < prog->len; i++) {
        free(prog->cmds[i].argStr);
    }
    free(prog->cmds);
    prog->len = 0;
}

// appends a command to the program structure
State addCommand(Program *prog, Command *cmd) {
    prog->len++;

    Command *p = realloc(prog->cmds, sizeof(Command) * prog->len);
    if (p == NULL)
        return ERR_MEMORY;
    prog->cmds = p;

    size_t last = prog->len - 1;
    // the name is not needed for running, copied just for debugging purposes
    strcpy(prog->cmds[last].name, cmd->name);
    prog->cmds[last].fn = cmd->fn;
    // not set yet
    prog->cmds[last].argStr = NULL;
    return SUCCESS;
}

// ---------- ARGUMENTS FUNCTIONS ------------

// TODO might not be needed
State arguments_ctor(Arguments *args) {
    args->delimiters = NULL;
    args->filename = NULL;
    args->commandString = NULL;
    return SUCCESS;
}


// reads delimiters from arguments
State parseArguments(int argc, char **argv, Arguments *args) {
    if (argc < 2)
        return ERR_BAD_SYNTAX;

    int i = 1;
    // reading delimiters
    if (strcmp("-d", argv[i]) == 0) {
        if (++i >= argc)
            return ERR_BAD_SYNTAX;

        args->delimiters = malloc(strlen(argv[i]) + 1);
        if (args->delimiters == NULL)
            return ERR_MEMORY;

        strcpy(args->delimiters, argv[i]);
        if (++i >= argc)
            return ERR_BAD_SYNTAX;
    } else {
        args->delimiters = malloc(2 * sizeof(char));
        if (args->delimiters == NULL)
            return ERR_MEMORY;

        strcpy(args->delimiters, " ");
    }

    // reading commands from file
    if (strcmp("-c", argv[i]) == 0) {
        if (++i >= argc)
            return ERR_BAD_SYNTAX;

        // file with commands
        FILE *fp = fopen(argv[i], "r");
        if (!fp)
            return ERR_FILE_ACCESS;

        args->commandString = fileToBuffer(fp);
        if (args->commandString == NULL)
            return ERR_MEMORY;

        if (++i >= argc)
            return ERR_BAD_SYNTAX;
    } else {
        // reading commands from the argument
        args->commandString = malloc(strlen(argv[i]) + 1);
        if (args->commandString == NULL)
            return ERR_MEMORY;

        strcpy(args->commandString, argv[i]);

        if (++i >= argc)
            return ERR_BAD_SYNTAX;
    }

    args->filename = malloc(strlen(argv[i]) + 1);
    if (args->filename == NULL)
        return ERR_MEMORY;

    strcpy(args->filename, argv[i]);

    // if there are any arguments left at this point,
    // the syntax must be wrong
    if (i < argc-1)
        return ERR_BAD_SYNTAX;

    return SUCCESS;
}

// ---------- VARIABLES FUNCTIONS -----------

State variables_ctor(Variables *v) {
    State s = SUCCESS;
    selection_init(&v->selVar);

    const int num = sizeof(v->cellVars) / sizeof(Cell);
    for (int i=0; i<num; i++) {
        s = cell_ctor(&v->cellVars[i]);
        if (s != SUCCESS)
            break;
    }
    return s;
}

void variables_dtor(Variables *v) {
    const int num = sizeof(v->cellVars) / sizeof(Cell);
    for (int i=0; i<num; i++) {
        cell_dtor(&v->cellVars[i]);
    }
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

// ---------- INTERFACE FUNCTIONS -----------
// these functions are interface between raw data and command functions
// all addresses are in range from 1

// make sure, that the coordinates up to these can be accessed
State assureTableSize(Table *table, unsigned rows, unsigned cols) {
    State s = SUCCESS;
    // Add columns to table, until they at least match
    while (cols >= table->cols) {
        s = addCol(table);
        if (s != SUCCESS)
            return s;
    }
    // Do the same for rows
    while (rows >= table->rows) {
        s = addRow(table);
        if (s != SUCCESS)
            return s;
    }
    return SUCCESS;
}

// ---------- COMMAND FUNCTIONS -----------
// the functions, that execute the actual commands

State test_cmd(Context ctx) {
    fprintf(stderr, "test executed with arguments '%s'\n", ctx.argStr);
    return SUCCESS;
}

State print_cmd(Context ctx) {
    printTable(ctx.table, stderr);
    return SUCCESS;
}

State irow_cmd(Context ctx) {
    if (strcmp(ctx.argStr, ""))
        return ERR_BAD_SYNTAX;

    return SUCCESS;
}

State arow_cmd(Context ctx) {
    if (strcmp(ctx.argStr, ""))
        return ERR_BAD_SYNTAX;

    return SUCCESS;
}

// ---------- MORE COMPLEX FUNCTIONS -----------

// Reads table from stdin and saves it into the table structure
// The function also reads delimiters from arguments
// Returns program state
// Expects an empty table
State readTable(Table *table, FILE *f, char *delimiters) {
    // set the table's main delimiter
    table->delim = delimiters[0];

    char *fileBuffer = fileToBuffer(f);
    if (fileBuffer == NULL)
        return ERR_MEMORY;

    State s = SUCCESS;
    // current row and column
    unsigned row=0, col=0;
    size_t i = 0;

    while (true) {
        char cellBuffer[MAX_CELL_LENGTH];
        size_t shift = parseString(cellBuffer, &fileBuffer[i], delimiters);
        // +1 to skip the delimiter
        i += shift + 1;

        // if there is nothing left
        if (fileBuffer[i-1] == '\0')
            break;

        // write to table
        s = assureTableSize(table, row, col);
        if (s != SUCCESS)
            return s;

        s = writeCell(&table->cells[row][col], cellBuffer);
        if (s != SUCCESS)
            return s;

        if (fileBuffer[i-1] == '\n') {
            row++;
            col = 0;
            continue;
        }

        if (strchr(delimiters, fileBuffer[i-1])) {
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
            printCell(table, &table->cells[i][j], f);

            if (j < table->cols - 1)
                fputc(table->delim, f);
            else
                fputc('\n', f);
        }
    }
}

State parseCommands(Program *prog, char *cmdStr) {
    // command delimiters
    char *delims = ";";
    Command knownCommands[] = {
        {.name="test", .fn=test_cmd}, // TODO delete later?
        {.name="print", .fn=print_cmd},
        {.name="irow", .fn=irow_cmd},
        {.name="arow", .fn=arow_cmd}
    };
    const int NUM_KNOWN_CMDS = sizeof(knownCommands) / sizeof(Command);
    // the imaginary reading head of cmdStr
    int strIndex=0;

    while (true) {
        bool found = false;
        // first, check, if the command is select
        // these can be written with weird syntax, when they start with '['
        // if (cmdStr[i] == '[') {}

        // check for command
        if (!found) {
            for (int j=0; j < NUM_KNOWN_CMDS; j++) {
                // if the command is found at the beginning of current index
                if (strbgn(&cmdStr[strIndex], knownCommands[j].name) == 0) {
                    strIndex += strlen(knownCommands[j].name);
                    addCommand(prog, &knownCommands[j]);
                    found = true;
                    break;
                }
            }
        }
        if (!found)
            return ERR_BAD_COMMANDS;
        // buffer for command's arguments
        char argBuf[MAX_COMMAND_LENGTH];
        // this might cause some issues later, now commands can be in
        // quotation marks and escaping is allowed
        size_t shift = parseString(argBuf, &cmdStr[strIndex], delims);
        // to make the next line cleaner
        Command *lastCmdPtr = &prog->cmds[prog->len - 1];
        lastCmdPtr->argStr = malloc((strlen(argBuf) + 1) * sizeof(char));
        if (lastCmdPtr->argStr == NULL)
            return ERR_MEMORY;
        strcpy(lastCmdPtr->argStr, argBuf);

        // +1 to skip the delimiter
        strIndex += shift + 1;
        // if there is delimiter, there maust be another command
        if (cmdStr[strIndex-1] == ';')
            continue;
        // if there is nothing left
        if (cmdStr[strIndex-1] == '\0')
            break;
    }
    return SUCCESS;
}

State executeProgram(Program *prog, Table *table) {
    State s;
    State (*function)(Context);

    Variables variables;
    variables_ctor(&variables);
    // set context, that doesn't change with each command
    Context context = {.table=table, .vars=&variables};

    for (size_t i=0; i < prog->len; i++) {
        // set the correct function
        function = prog->cmds[i].fn;
        // set up the context before executing
        context.argStr = prog->cmds[i].argStr;

        s = function(context);
        if (s != SUCCESS)
            break;
    }
    variables_dtor(&variables);
    return s;
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

    // all arguments are parsed into this structure
    Arguments arguments;

    // all the commands in dynamic array of Command objects
    // this can be directly executed
    Program program;
    program_ctor(&program);

    if (s == SUCCESS)
        s = parseArguments(argc, argv, &arguments);

    // parse the commands, so the memory can be freed
    if (s == SUCCESS)
        s = parseCommands(&program, arguments.commandString);
    free(arguments.commandString);

    // open the file where the table is stored
    if (s == SUCCESS) {
        fp = fopen(arguments.filename, "r+");
        if (!fp)
            s = ERR_FILE_ACCESS;
    }
    free(arguments.filename);

    // reading the table
    if (s == SUCCESS)
        s = readTable(&table, fp, arguments.delimiters);
    free(arguments.delimiters);

    // executing commands
    if (s == SUCCESS)
        s = executeProgram(&program, &table);

    // TODO uncomment to change the actual table
    // if (s == SUCCESS)
    //     printTable(&table, fp);

    // if file is opened, close it
    if (fp)
        fclose(fp);
    program_dtor(&program);
    table_dtor(&table);
    printErrorMessage(s);
    return s;
}
