/**
 * Author: Ondrej Mach
 * VUT login: xmacho12
 * E-mail: ondrej.mach@seznam.cz
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define MAX_CELL_LENGTH 1001
#define MAX_COMMAND_LENGTH 1001
#define MAX_FILENAME_LENGTH 1001
#define MAX_DELIMITERS 1001
#define INF_CYCLE_LIMIT 1000000

// struct for each cell
// might not be necessary, but makes the program more extensible
typedef struct {
    char *str;
} Cell;

// selection is always a rectangle
typedef struct {
    // every of these can be zero
    // that means that the selection goes to the end of the table
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
    Selection sel;
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
    ERR_GENERIC,
    ERR_TOO_LONG,
    ERR_BAD_SELECTION,
    ERR_BAD_SYNTAX,
    ERR_BAD_COMMANDS,
    ERR_BAD_FORMAT,
    ERR_TABLE_EMPTY,
    ERR_FILE_ACCESS,
    ERR_BAD_ORDER,
    ERR_BAD_TABLE,
    ERR_MEMORY,
    ERR_INF_CYCLE,
} State;

// Everything, that commands might have access to
// is easier to extend, when a command needs something special
typedef struct {
    Table *table;
    // arguments ot the command itself
    char *argStr;
    Variables *vars;
    // be very careful, this influences the flow of the program
    unsigned *execPtr;
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
    unsigned len;
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

unsigned selUpperBound(Table *table);
unsigned selLowerBound(Table *table);
unsigned selLeftBound(Table *table);
unsigned selRightBound(Table *table);

State assureTableSize(Table *table, unsigned rows, unsigned cols);

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

State parseCoords(char *str, unsigned *a, unsigned *b) {
    if (str[0] != '[')
        return ERR_BAD_SYNTAX;

    // shift for '[' at the beginning
    unsigned strIndex = 1;

    char *endPtr;
    int shift;

    *a = (unsigned)strtol(&str[strIndex], &endPtr, 10);
    shift = endPtr - &str[strIndex];
    strIndex += shift;
    if (shift == 0)
        return ERR_BAD_SYNTAX;

    if (str[strIndex] != ',')
        return ERR_BAD_SYNTAX;
    strIndex += 1;

    *b = (unsigned)strtol(&str[strIndex], &endPtr, 10);
    shift = endPtr - &str[strIndex];
    strIndex += shift;
    if (shift == 0)
        return ERR_BAD_SYNTAX;

    if (strcmp(&str[strIndex], "]") != 0)
        return ERR_BAD_SYNTAX;

    if (*a == 0 || *b == 0)
        return ERR_BAD_SYNTAX;

    return SUCCESS;
}

State parseSelection(Selection *sel, char *str) {
    if (str[0] != '[')
        return ERR_BAD_SYNTAX;

    const unsigned MAX_NUM = 4;
    unsigned numValues = 0;
    unsigned values[MAX_NUM];

    // shift for '[' at the beginning
    unsigned strIndex = 1;
    while (numValues < MAX_NUM) {
        if (str[strIndex] == '_') {
            values[numValues] = 0;
            strIndex += 1;
        } else {
            char *endPtr;
            values[numValues] = (unsigned)strtol(&str[strIndex], &endPtr, 10);
            int shift = endPtr - &str[strIndex];

            if (shift == 0)
                return ERR_BAD_SYNTAX;

            strIndex += shift;
        }
        numValues++;

        if (str[strIndex] == ',') {
            strIndex++;
            continue;
        }

        if (str[strIndex] == ']')
            break;

        return ERR_BAD_SYNTAX;
    }

    if (numValues == 2) {
        sel->startRow = values[0];
        sel->endRow = values[0];
        sel->startCol = values[1];
        sel->endCol = values[1];
        return SUCCESS;
    }

    if (numValues == 4) {
        sel->startRow = values[0];
        sel->endRow = values[2];
        sel->startCol = values[1];
        sel->endCol = values[3];
        return SUCCESS;
    }

    return ERR_BAD_SYNTAX;
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
    // shallow copy is enough, pointers will be swapped
    tmp = *c2;
    *c2 = *c1;
    *c1 = tmp;

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

// gets cell pointer from its coordinates
double cellToDouble(Cell *cell) {
    char *end;
    double val = strtod(cell->str, &end);

    if (end == cell->str)
        return NAN;

    return val;
}

// gets cell pointer from its coordinates
Cell *getCellPtr(Table *table, unsigned row, unsigned col) {
    if ((row == 0) || (col == 0))
        return NULL;

    assureTableSize(table, row, col);
    return &table->cells[row-1][col-1];
}

// returns NULL if there is more than one cell selected
Cell *selectedCell(Table *table) {
    unsigned row = selUpperBound(table);
    unsigned col = selLeftBound(table);

    if (row != selLowerBound(table))
        return NULL;

    if (col != selRightBound(table))
        return NULL;

    return getCellPtr(table, row, col);
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
State addCommand(Program *prog, const Command *cmd) {
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
    selection_init(&table->sel);
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

// ---------- SELECTION FUNCTIONS -----------
// functions don't have to access the data directly
// program is more extensible this way

unsigned selUpperBound(Table *table) {
    if (table->sel.startRow == 0) {
        return 1;
    }
    return table->sel.startRow;
}

unsigned selLowerBound(Table *table) {
    if (table->sel.endRow == 0) {
        return table->rows;
    }
    return table->sel.endRow;
}

unsigned selLeftBound(Table *table) {
    if (table->sel.startCol == 0) {
        return 1;
    }
    return table->sel.startCol;
}

unsigned selRightBound(Table *table) {
    if (table->sel.endCol == 0) {
        return table->cols;
    }
    return table->sel.endCol;
}

// select one cell
State selectCell(Table *table, unsigned row, unsigned col) {
    table->sel.startRow = row;
    table->sel.endRow = row;
    table->sel.startCol = col;
    table->sel.endCol = col;

    return SUCCESS;
}

State selectMinMax(Table *table, bool max) {
    double extreme = max ? -INFINITY : INFINITY;
    unsigned extremeRow = 0;
    unsigned extremeCol = 0;
    // go through every selected cell
    for (unsigned i=selUpperBound(table); i <= selLowerBound(table); i++) {
        for (unsigned j=selLeftBound(table); j <= selRightBound(table); j++) {
            Cell *cellPtr = getCellPtr(table, i, j);
            double value = cellToDouble(cellPtr);
            if (isnan(value))
                continue;

            bool found = max ? (value > extreme) : (value < extreme);

            if (found) {
                extreme = value;
                extremeRow = i;
                extremeCol = j;
            }
        }
    }
    // if an extreme is found, set the selection on it
    if (extremeRow != 0)
        selectCell(table, extremeRow, extremeCol);
    // otherwise leave the selection as is
    return SUCCESS;
}

// ---------- INTERFACE FUNCTIONS -----------
// these functions are interface between raw data and command functions
// all addresses use user addressing for cells

// make sure, that the coordinates up to these can be accessed
State assureTableSize(Table *table, unsigned rows, unsigned cols) {
    State s = SUCCESS;
    // Add columns to table, until they at least match
    while (cols > table->cols) {
        s = addCol(table);
        if (s != SUCCESS)
            return s;
    }
    // Do the same for rows
    while (rows > table->rows) {
        s = addRow(table);
        if (s != SUCCESS)
            return s;
    }
    return SUCCESS;
}

// swap rows of table, user coordinates
State swapRows(Table *table, unsigned r1, unsigned r2) {
    // convert to real addressing
    r1--;
    r2--;

    // swap around the pointers
    Cell *tmp;
    tmp = table->cells[r2];
    table->cells[r2] = table->cells[r1];
    table->cells[r1] = tmp;
    return SUCCESS;
}

// moves row while shifting the others
State moveRow(Table *table, unsigned start, unsigned end) {
    // convert to real addressing
    start--;
    end--;

    unsigned i = start;

    int direction;
    if (i < end)
        direction = 1;
    else
        direction = -1;

    Cell *tmp = table->cells[start];

    while (i != end) {
        table->cells[i] = table->cells[i + direction];
        i += direction;
    }

    table->cells[end] = tmp;
    return SUCCESS;
}

// swap rows of table, user coordinates
State swapCols(Table *table, unsigned c1, unsigned c2) {
    // convert to real addressing
    c1--;
    c2--;
    // for each line of the table
    for (unsigned i=0; i<table->rows; i++) {
        swapCell(&table->cells[i][c1], &table->cells[i][c2]);
    }
    return SUCCESS;
}

// moves row while shifting the others
State moveCol(Table *table, unsigned start, unsigned end) {
    unsigned i = start;

    int direction;
    if (i < end)
        direction = 1;
    else
        direction = -1;

    while (i != end) {
        swapCols(table, i, i + direction);
        i += direction;
    }
    return SUCCESS;
}

State sumCountSelected(Table *table, double *sum, unsigned *count) {
    *sum = 0;
    *count = 0;
    // go through every selected cell
    for (unsigned i=selUpperBound(table); i <= selLowerBound(table); i++) {
        for (unsigned j=selLeftBound(table); j <= selRightBound(table); j++) {
            Cell *cellPtr = getCellPtr(table, i, j);
            double value = cellToDouble(cellPtr);
            if (isnan(value))
                continue;

            *sum += value;
            (*count)++;
        }
    }
    return SUCCESS;
}

// ---------- COMMAND FUNCTIONS -----------
// the functions, that execute the actual commands
// they all have the same interface (patrameter is Context, return is State)
// all the functions also have _cmd just to signify this

// prints context variables into stderr
State dump_cmd(Context ctx) {
    // if argument is not empty
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;

    fprintf(stderr, "Context dump:\nVariables:\n");

    for(int i=1; i<=9; i++)
        fprintf(stderr, "\t_%d = '%s'\n", i, ctx.vars->cellVars[i].str);

    fprintf(stderr, "\t_ = rows %d to %d, cols %d to %d\n",
        ctx.vars->selVar.startRow,
        ctx.vars->selVar.endRow,
        ctx.vars->selVar.startCol,
        ctx.vars->selVar.endRow
    );

    fprintf(stderr, "Active selection: rows %d to %d, cols %d to %d\n",
        ctx.table->sel.startRow,
        ctx.table->sel.endRow,
        ctx.table->sel.startCol,
        ctx.table->sel.endRow
    );

    fprintf(stderr, "Execution pointer: %d\n", *(ctx.execPtr));
    return SUCCESS;
}

// prints the table into stderr
State print_cmd(Context ctx) {
    // if argument is not empty
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;

    printTable(ctx.table, stderr);
    return SUCCESS;
}

// selects the lowest nuber from selected cells
State selectMin_cmd(Context ctx) {
    return selectMinMax(ctx.table, false);
}

// selects the highest nuber from selected cells
State selectMax_cmd(Context ctx) {
    return selectMinMax(ctx.table, true);
}

// Command to select cells from coordinates
State selectCoords_cmd(Context ctx) {
    if (ctx.argStr[0] != '[')
        return ERR_BAD_SYNTAX;

    const unsigned MAX_NUM = 4;
    unsigned numValues = 0;
    unsigned values[MAX_NUM];

    // shift for '[' at the beginning
    unsigned strIndex = 1;
    while (numValues < MAX_NUM) {
        if (ctx.argStr[strIndex] == '_') {
            values[numValues] = 0;
            strIndex += 1;
        } else {
            char *endPtr;
            values[numValues] = (unsigned)strtol(&ctx.argStr[strIndex], &endPtr, 10);
            int shift = endPtr - &ctx.argStr[strIndex];

            if (shift == 0)
                return ERR_BAD_SYNTAX;

            strIndex += shift;
        }
        numValues++;

        if (ctx.argStr[strIndex] == ',') {
            strIndex++;
            continue;
        }

        if (ctx.argStr[strIndex] == ']')
            break;

        return ERR_BAD_SYNTAX;
    }

    if ((numValues == 1) && (values[0] == 0)) {
        // apply the selection variable from memory
        ctx.table->sel = ctx.vars->selVar;
    }

    if (numValues == 2) {
        selectCell(ctx.table, values[0], values[1]);
        return SUCCESS;
    }

    if (numValues == 4) {
        ctx.table->sel.startRow = values[0];
        ctx.table->sel.endRow = values[2];
        ctx.table->sel.startCol = values[1];
        ctx.table->sel.endCol = values[3];
        return SUCCESS;
    }

    return ERR_BAD_SYNTAX;
}

// applies selection variable to the table
State selectStore_cmd(Context ctx) {
    ctx.vars->selVar = ctx.table->sel;
    return SUCCESS;
}

// applies selection variable to the table
State selectLoad_cmd(Context ctx) {
    ctx.table->sel = ctx.vars->selVar;
    return SUCCESS;
}

// select the first cell, where str argument matches
State selectFind_cmd(Context ctx) {
    // copy original string
    char searchStr[strlen(ctx.argStr) + 1];
    strcpy(searchStr, ctx.argStr);
    // remove the ] at the end
    searchStr[strlen(ctx.argStr)-1] = '\0';

    // go through every selected cell
    for (unsigned i=selUpperBound(ctx.table); i <= selLowerBound(ctx.table); i++) {
        for (unsigned j=selLeftBound(ctx.table); j <= selRightBound(ctx.table); j++) {
            Cell *cellPtr = getCellPtr(ctx.table, i, j);
            if (strcmp(cellPtr->str, searchStr) == 0) {
                selectCell(ctx.table, i, j);
                return SUCCESS;
            }
        }
    }
    return SUCCESS;
}

// Layout commands

// appends a row after the lower bound of the selection
State arow_cmd(Context ctx) {
    // if argument is not empty
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;

    State s;
    s = addRow(ctx.table);

    if (s == SUCCESS) {
        // start on the last line
        unsigned start = ctx.table->rows;
        // and swap until you get to the lower bound of selection
        unsigned end = selLowerBound(ctx.table) + 1;
        // move the empty row up to the selection
        s = moveRow(ctx.table, start, end);
    }
    return s;
}

// TODO could be one function with arow
// inserts a row right above the selected region
State irow_cmd(Context ctx) {
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;

    State s;
    s = addRow(ctx.table);

    if (s == SUCCESS) {
        // start on the last line
        unsigned start = ctx.table->rows;
        // and swap until you get to the top of selection
        unsigned end = selUpperBound(ctx.table);
        // move the empty row up to the selection
        s = moveRow(ctx.table, start, end);
    }
    return s;
}

// deletes selected rows
State drow_cmd(Context ctx) {
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;

    State s;
    // how many lines we need to delete
    unsigned toDelete = selLowerBound(ctx.table) - selUpperBound(ctx.table) + 1;

    for (unsigned i=0; i<toDelete; i++) {
        // move the line to the last place in table
        unsigned start = selUpperBound(ctx.table);
        unsigned end = ctx.table->rows;
        s = moveRow(ctx.table, start, end);
        if (s != SUCCESS)
            break;
        // then delete it
        deleteRow(ctx.table);
    }
    return s;
}

// appends an empty column after selected cells
State acol_cmd(Context ctx) {
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;

    State s;
    s = addCol(ctx.table);

    if (s == SUCCESS) {
        unsigned start = ctx.table->cols;
        unsigned end = selRightBound(ctx.table) + 1;
        s = moveCol(ctx.table, start, end);
    }
    return s;
}

// inserts an empty column left from the selected cells
State icol_cmd(Context ctx) {
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;

    State s;
    s = addCol(ctx.table);

    if (s == SUCCESS) {
        unsigned start = ctx.table->cols;
        unsigned end = selLeftBound(ctx.table);
        s = moveCol(ctx.table, start, end);
    }
    return s;
}

// deletes selected columns
State dcol_cmd(Context ctx) {
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;

    State s;
    // how many lines we need to delete
    unsigned toDelete = selRightBound(ctx.table) - selLeftBound(ctx.table) + 1;

    for (unsigned i=0; i<toDelete; i++) {
        // move the line to the last place in table
        unsigned start = selLeftBound(ctx.table);
        unsigned end = ctx.table->cols;
        s = moveCol(ctx.table, start, end);
        if (s != SUCCESS)
            break;
        // then delete it
        deleteCol(ctx.table);
    }
    return s;
}

State set_cmd(Context ctx) {
    // go through every selected cell
    for (unsigned i=selUpperBound(ctx.table); i <= selLowerBound(ctx.table); i++) {
        for (unsigned j=selLeftBound(ctx.table); j <= selRightBound(ctx.table); j++) {
            Cell *cellPtr = getCellPtr(ctx.table, i, j);
            State s = writeCell(cellPtr, ctx.argStr);
            if (s != SUCCESS)
                return s;
        }
    }
    return SUCCESS;
}

State clear_cmd(Context ctx) {
    if (ctx.argStr[0] != '\0')
        return ERR_BAD_SYNTAX;
    // go through every selected cell
    for (unsigned i=selUpperBound(ctx.table); i <= selLowerBound(ctx.table); i++) {
        for (unsigned j=selLeftBound(ctx.table); j <= selRightBound(ctx.table); j++) {
            Cell *cellPtr = getCellPtr(ctx.table, i, j);
            State s = writeCell(cellPtr, "");
            if (s != SUCCESS)
                return s;
        }
    }
    return SUCCESS;
}

// Data commands

// swaps selected cell with the cell specified by argument
State swap_cmd(Context ctx) {
    unsigned row, col;
    State s = parseCoords(ctx.argStr, &row, &col);
    if (s != SUCCESS)
        return s;

    Cell *c1 = getCellPtr(ctx.table, row, col);
    Cell *c2 = selectedCell(ctx.table);

    if (c1 == NULL)
        return ERR_BAD_SYNTAX;

    if (c2 == NULL)
        return ERR_BAD_SELECTION;

    swapCell(c1, c2);
    return SUCCESS;
}

State sum_cmd(Context ctx) {
    unsigned row, col;
    State s = parseCoords(ctx.argStr, &row, &col);
    if (s != SUCCESS)
        return s;

    double sum;
    unsigned count;
    sumCountSelected(ctx.table, &sum, &count);

    char buffer[MAX_CELL_LENGTH];
    sprintf(buffer, "%g", sum);

    Cell *resultCell = getCellPtr(ctx.table, row, col);
    writeCell(resultCell, buffer);

    return SUCCESS;
}

State avg_cmd(Context ctx) {
    unsigned row, col;
    State s = parseCoords(ctx.argStr, &row, &col);
    if (s != SUCCESS)
        return s;

    double sum;
    unsigned count;
    sumCountSelected(ctx.table, &sum, &count);
    char buffer[MAX_CELL_LENGTH];

    sprintf(buffer, "%g", sum / count);
    Cell *resultCell = getCellPtr(ctx.table, row, col);
    writeCell(resultCell, buffer);
    return SUCCESS;
}

State count_cmd(Context ctx) {
    unsigned row, col;
    State s = parseCoords(ctx.argStr, &row, &col);
    if (s != SUCCESS)
        return s;

    unsigned count=0;

    // go through every selected cell
    for (unsigned i=selUpperBound(ctx.table); i <= selLowerBound(ctx.table); i++) {
        for (unsigned j=selLeftBound(ctx.table); j <= selRightBound(ctx.table); j++) {
            Cell *cellPtr = getCellPtr(ctx.table, i, j);

            if (strcmp(cellPtr->str, ""))
                count++;
        }
    }
    char buffer[MAX_CELL_LENGTH];
    sprintf(buffer, "%d", count);
    Cell *resultCell = getCellPtr(ctx.table, row, col);
    writeCell(resultCell, buffer);
    return SUCCESS;
}

State len_cmd(Context ctx) {
    unsigned row, col;
    State s = parseCoords(ctx.argStr, &row, &col);
    if (s != SUCCESS)
        return s;

    Cell *measuredCell = selectedCell(ctx.table);
    size_t len = strlen(measuredCell->str);

    char buffer[MAX_CELL_LENGTH];
    sprintf(buffer, "%lu", len);

    Cell *resultCell = getCellPtr(ctx.table, row, col);
    writeCell(resultCell, buffer);
    return SUCCESS;
}

// Variable commands

State def_cmd(Context ctx) {
    int n = ctx.argStr[0] - '0';
    if ((ctx.argStr[1] != '\0') || (n < 0) || (n > 9))
        return ERR_BAD_SYNTAX;

    Cell *src = selectedCell(ctx.table);
    if (src == NULL)
        return ERR_BAD_SELECTION;
    deepCopyCell(&ctx.vars->cellVars[n], src);

    return SUCCESS;
}

State use_cmd(Context ctx) {
    int n = ctx.argStr[0] - '0';
    if ((ctx.argStr[1] != '\0') || (n < 0) || (n > 9))
        return ERR_BAD_SYNTAX;

    Cell *dst = selectedCell(ctx.table);
    if (dst == NULL)
        return ERR_BAD_SELECTION;
    deepCopyCell(dst, &ctx.vars->cellVars[n]);

    return SUCCESS;
}

State inc_cmd(Context ctx) {
    int n = ctx.argStr[0] - '0';
    if ((ctx.argStr[1] != '\0') || (n < 0) || (n > 9))
        return ERR_BAD_SYNTAX;

    // variable to increment
    Cell *cellPtr = &ctx.vars->cellVars[n];

    double value = cellToDouble(cellPtr);

    if (isnan(value)) {
        writeCell(cellPtr, "1");
        return SUCCESS;
    }

    char buffer[MAX_CELL_LENGTH];
    sprintf(buffer, "%g", value + 1);

    writeCell(cellPtr, buffer);
    return SUCCESS;
}

// Control commands

State goto_cmd(Context ctx) {
    char *endPtr;
    int steps = strtol(ctx.argStr, &endPtr, 10);

    if (*endPtr != '\0')
        return ERR_BAD_SYNTAX;

    // goto +1 should have no effect
    *ctx.execPtr += steps - 1;
    return SUCCESS;
}

State iszero_cmd(Context ctx) {
    int n = ctx.argStr[0] - '0';
    if ((ctx.argStr[1] != ' ') || (n < 0) || (n > 9))
        return ERR_BAD_SYNTAX;

    // points at varible that needs to be checked
    Cell *cellPtr = &ctx.vars->cellVars[n];

    char *endPtr;
    long steps = strtol(&ctx.argStr[2], &endPtr, 10);
    if (*endPtr != '\0')
        return ERR_BAD_SYNTAX;

    if (strcmp(cellPtr->str, "0") == 0)
        *ctx.execPtr += steps - 1;

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
        s = assureTableSize(table, row+1, col+1);
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

// takes commands as a string
// and writes them into the program structure
State parseCommands(Program *prog, char *cmdStr) {
    // command delimiters
    char *delims = ";";
    // all commands except selection because of their weird syntax
    const Command knownCommands[] = {
        // Custom commands (not in official specification)
        {.name="print", .fn=print_cmd},
        {.name="dump", .fn=dump_cmd},
        // Selection
        {.name="[min]", .fn=selectMin_cmd},
        {.name="[max]", .fn=selectMax_cmd},
        {.name="[find ", .fn=selectFind_cmd}, // cannot work with STR, maybe TODO later?
        {.name="[_]", .fn=selectStore_cmd},
        // Layout commands
        {.name="irow", .fn=irow_cmd},
        {.name="arow", .fn=arow_cmd},
        {.name="drow", .fn=drow_cmd},
        {.name="icol", .fn=icol_cmd},
        {.name="acol", .fn=acol_cmd},
        {.name="dcol", .fn=dcol_cmd},
        // Data commands
        {.name="set ", .fn=set_cmd},
        {.name="clear", .fn=clear_cmd},
        {.name="swap ", .fn=swap_cmd},
        {.name="sum ", .fn=sum_cmd},
        {.name="avg ", .fn=avg_cmd},
        {.name="count ", .fn=count_cmd},
        {.name="len ", .fn=len_cmd},
        // Variable commands
        {.name="def _", .fn=def_cmd},
        {.name="use _", .fn=use_cmd},
        {.name="inc _", .fn=inc_cmd},
        {.name="[set]", .fn=selectLoad_cmd},
        // Control commands
        {.name="goto ", .fn=goto_cmd},
        {.name="iszero _", .fn=iszero_cmd},
        //{.name="sub ", .fn=goto_cmd},
    };
    const int NUM_KNOWN_CMDS = sizeof(knownCommands) / sizeof(Command);
    // the imaginary reading head of cmdStr
    int strIndex=0;

    while (true) {
        bool found = false;
        // check for command
        for (int j=0; j < NUM_KNOWN_CMDS; j++) {
            // if the command is found at the beginning of current index
            if (strbgn(&cmdStr[strIndex], knownCommands[j].name) == 0) {
                strIndex += strlen(knownCommands[j].name);
                addCommand(prog, &knownCommands[j]);
                found = true;
                break;
            }
        }
        // check for the weird coordinate selection command
        if (!found && (cmdStr[strIndex] == '[')) {
            const Command select = {.fn=selectCoords_cmd};
            addCommand(prog, &select);
            found = true;
            // strIndex doesn't shift
            // because the command is its own argument if it makes any sense
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

// takes in program structure and executes commands in it
State executeProgram(Program *prog, Table *table) {
    State s;
    State (*function)(Context);

    Variables variables;
    variables_ctor(&variables);
    // set context, that doesn't change with each command
    unsigned i;
    Context context = {.table=table, .vars=&variables, .execPtr=&i};

    unsigned realCounter = 0;

    for (i=0; i < prog->len; i++) {
        // set the correct function
        function = prog->cmds[i].fn;
        // set up the context before executing
        context.argStr = prog->cmds[i].argStr;

        s = function(context);
        if (s != SUCCESS)
            break;

        if (realCounter++ >= INF_CYCLE_LIMIT) {
            s = ERR_INF_CYCLE;
            break;
        }
    }
    variables_dtor(&variables);
    return s;
}

// prints basic help on how to use the program
void printUsage() {
    const char *usageString = "\nUsage:\n"
        "./sps [-d DELIM] [Commands for editing the table]\n";

    fprintf(stderr, "%s", usageString);
}

// prints error message according to the error state
void printErrorMessage(State err_state) {
    switch(err_state) {
        case SUCCESS:
            break;

        case ERR_GENERIC:
            fputs("Generic error\n", stderr);
            break;

        case ERR_TOO_LONG:
            fputs("Maximum file size is 10kiB\n", stderr);
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
