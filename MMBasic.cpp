/*
 * MMBasic.c - MMBasic Core Implementation
 * 
 * This file implements the core MMBasic interpreter for M5Cardputer
 */

#include "MMBasic.h"
#include "HAL.h"
#include <Wire.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

// Forward declarations for internal functions
char *MMBasic_GetTempString(void);
static int EvaluateExpression(char **expr, int *itype, int *ival, float *fval, char **sval);
static int EvaluateSubExpression(char **expr, int *itype, int *ival, float *fval, char **sval);
static int EvaluateTerm(char **expr, int *itype, int *ival, float *fval, char **sval);
static int EvaluateFactor(char **expr, int *itype, int *ival, float *fval, char **sval);
static int EvaluateFunction(char **expr, int funcToken, int *itype, int *ival, float *fval, char **sval);
static void MMBasic_ExecuteDispatch(char *line);

// Global variables
char *progmem = NULL;           // Program memory
int progsize = 0;               // Program size
char *progptr = NULL;           // Current program pointer
char inpbuf[STRINGSIZE];        // Input buffer
char tknbuf[STRINGSIZE];        // Token buffer
struct s_vartbl *vartbl = NULL; // Variable table
int varcnt = 0;                 // Variable count
struct s_subfun subfun[MAXSUBFUN]; // Subroutine table
int subfunct = 0;               // Subroutine count
jmp_buf mark;                   // Error recovery jump buffer
int MMCharPos = 0;              // Character position
volatile int MMAbort = 0;       // Abort flag
char BreakKey = 3;              // Break key (Ctrl-C)

// Current line being executed
char *currentLine = NULL;
int currentLineIndex = -1;
bool flowControlActive = false; // Set true when a command changes line flow
bool traceOn = false;            // TRACE ON/OFF
int MMBasic_ForSingleLine = 0;  // Set true when line has FOR...NEXT on same line

// FOR loop stack
struct s_forstack {
    int lineIdx;                // Index in lines[] after the FOR line (loop body start)
    char varname[MAXVARLEN+1];  // Loop variable name
    int varindex;               // Variable index
    int toval;                  // TO value
    int stepval;                // STEP value
};
struct s_forstack forstack[MAXFORLOOPS];
int forstackptr = 0;

// GOSUB stack
int gosubstack[MAXGOSUB];
int gosubstackptr = 0;

// WHILE stack
int whilestack[MAXFORLOOPS];
int whilestackptr = 0;

// DO loop stack
int dostack[MAXFORLOOPS];
int dostackptr = 0;

// SELECT CASE stack
struct s_select {
    int matchValue;
    char matchStr[STRINGSIZE];  // For string SELECT CASE
    char matchType;             // T_INT or T_STR
    bool matched;
};
s_select selectstack[8];
int selectptr = 0;

// Program line storage
#define MAX_LINES 1000
char *lines[MAX_LINES];
int lineNumbers[MAX_LINES];
int linecnt = 0;

// Temporary string pool
char strpool[16][STRINGSIZE];
int strpoolindex = 0;

// File handle table (1-indexed, #1..#MAXOPENFILES)
struct s_filehandle {
    File file;
    bool inUse;
    bool isCom;        // true if COM port
    int comPort;       // COM port number (1-2)
};
s_filehandle fileTable[MAXOPENFILES + 1];

// Current drive (0 = A: flash, 1 = B: SD card)
// Cardputer only has B: (SD card), default to B:
int currentDrive = 1;

// DATA/READ/RESTORE state
int dataLineIdx = 0;
int dataOffset = 0;

// CONST tracking
bool *varIsConst = NULL;

// Sub/Fun table for SUB/FUNCTION support
struct s_subfun_entry {
    char name[33];
    int startLine, endLine, paramCount;
    char paramNames[8][33];
    bool isFunction;
    int returnLine;
};
s_subfun_entry subFunTable[128];
int subFunCount = 0;
int subFunCallReturnIdx = -1;

// Shadow stack for LOCAL variable support
struct s_shadow {
    int varIndex;
    char type;
    int ival;
    float fval;
    char *sval;
    int ndims;
    int dims[MAXDIMS];
    int *arr;
    bool wasConst;
    bool isStatic;
};
s_shadow shadowStack[128];
int shadowPtr = 0;
int shadowBase[16];
int shadowBaseSP = 0;
int subFunRetStack[16];

// Static variable storage and SUB tracking for LOCAL/STATIC support
struct s_static_var {
    char name[MAXVARLEN+1];
    int subfunIndex;
    char type;
    int ival;
    float fval;
    char sval[STRINGSIZE];
    int ndims;
    int dims[MAXDIMS];
    int *arr;
};
s_static_var staticVars[64];
int staticVarCount = 0;
int currentSubFunIdx = -1;
int subFunIdxStack[16];
int subFunIdxSP = 0;

// Token table
static const struct s_tokentbl commandtbl[] = {
    {"PRINT", C_CMD, C_PRINT, NULL},
    {"INPUT", C_CMD, C_INPUT, NULL},
    {"IF", C_CMD, C_IF, NULL},
    {"THEN", C_CMD, C_THEN, NULL},
    {"ELSE", C_CMD, C_ELSE, NULL},
    {"ENDIF", C_CMD, C_ENDIF, NULL},
    {"FOR", C_CMD, C_FOR, NULL},
    {"TO", C_CMD, C_TO, NULL},
    {"STEP", C_CMD, C_STEP, NULL},
    {"NEXT", C_CMD, C_NEXT, NULL},
    {"GOTO", C_CMD, C_GOTO, NULL},
    {"GOSUB", C_CMD, C_GOSUB, NULL},
    {"RETURN", C_CMD, C_RETURN, NULL},
    {"END", C_CMD, C_END, NULL},
    {"LET", C_CMD, C_LET, NULL},
    {"DIM", C_CMD, C_DIM, NULL},
    {"REM", C_CMD, C_REM, NULL},
    {"'", C_CMD, C_REM2, NULL},
    {"CLS", C_CMD, C_CLS, NULL},
    {"LOCATE", C_CMD, C_LOCATE, NULL},
    {"COLOR", C_CMD, C_COLOR, NULL},
    {"LINE", C_CMD, C_LINE, NULL},
    {"CIRCLE", C_CMD, C_CIRCLE, NULL},
    {"RECT", C_CMD, C_RECT, NULL},
    {"PIXEL", C_CMD, C_PIXEL, NULL},
    {"DATA", C_CMD, C_DATA, NULL},
    {"READ", C_CMD, C_READ, NULL},
    {"RESTORE", C_CMD, C_RESTORE, NULL},
    {"SAVE", C_CMD, C_SAVE, NULL},
    {"LOAD", C_CMD, C_LOAD, NULL},
    {"FILES", C_CMD, C_FILES, NULL},
    {"RUN", C_CMD, C_RUN, NULL},
    {"LIST", C_CMD, C_LIST, NULL},
    {"NEW", C_CMD, C_NEW, NULL},
    {"OPEN", C_CMD, C_OPEN, NULL},
    {"CLOSE", C_CMD, C_CLOSE, NULL},
    {"SEEK", C_CMD, C_SEEK, NULL},
    {"SWAP", C_CMD, C_SWAP, NULL},
    {"INC", C_CMD, C_INC, NULL},
    {"PAUSE", C_CMD, C_PAUSE, NULL},
    {"RANDOMIZE", C_CMD, C_RANDOMIZE, NULL},
    {"POKE", C_CMD, C_POKE, NULL},
    {"CONST", C_CMD, C_CONST, NULL},
    {"WHILE", C_CMD, C_WHILE, NULL},
    {"WEND", C_CMD, C_WEND, NULL},
    {"SELECT", C_CMD, C_SELECT, NULL},
    {"CASE", C_CMD, C_CASE, NULL},
    {"ENDSELECT", C_CMD, C_ENDSELECT, NULL},
    {"DO", C_CMD, C_DO, NULL},
    {"LOOP", C_CMD, C_LOOP, NULL},
    {"CONTINUE", C_CMD, C_CONTINUE, NULL},
    {"EXIT", C_CMD, C_EXIT, NULL},
    {"OPTION", C_CMD, C_OPTION, NULL},
    {"KILL", C_CMD, C_KILL, NULL},
    {"MKDIR", C_CMD, C_MKDIR, NULL},
    {"RMDIR", C_CMD, C_RMDIR, NULL},
    {"CHDIR", C_CMD, C_CHDIR, NULL},
    {"COPY", C_CMD, C_COPY, NULL},
    {"RENAME", C_CMD, C_RENAME, NULL},
    {"DRIVE", C_CMD, C_DRIVE, NULL},
    {"SETPIN", C_CMD, C_SETPIN, NULL},
    {"DIGOUT", C_CMD, C_DIGOUT, NULL},
    {"PWM", C_CMD, C_PWMCMD, NULL},
    {"I2C", C_CMD, C_I2C, NULL},
    {"SPI", C_CMD, C_SPI, NULL},
    {"IR", C_CMD, C_IR, NULL},
    {"SERVO", C_CMD, C_SERVO, NULL},
    {"PORT", C_CMD, C_PORTCMD, NULL},
    {"PULSE", C_CMD, C_PULSE, NULL},
    {"BOX", C_CMD, C_BOX, NULL},
    {"TEXT", C_CMD, C_TEXT, NULL},
    {"TRIANGLE", C_CMD, C_TRIANGLE, NULL},
    {"CLEAR", C_CMD, C_CLEAR, NULL},
    {"ERASE", C_CMD, C_ERASE, NULL},
    {"REDIM", C_CMD, C_REDIM, NULL},
    {"ERROR", C_CMD, C_ERROR, NULL},
    {"TRACE", C_CMD, C_TRACE, NULL},
    {"SORT", C_CMD, C_SORTCMD, NULL},
    {"ON", C_CMD, C_ON, NULL},
    {"SUB", C_CMD, C_SUB, NULL},
    {"FUNCTION", C_CMD, C_FUNCTION, NULL},
    {"CALL", C_CMD, C_CALL, NULL},
    {"ABS", C_FUNC, F_ABS, NULL},
    {"INT", C_FUNC, F_INT, NULL},
    {"SGN", C_FUNC, F_SGN, NULL},
    {"SQR", C_FUNC, F_SQR, NULL},
    {"SIN", C_FUNC, F_SIN, NULL},
    {"COS", C_FUNC, F_COS, NULL},
    {"TAN", C_FUNC, F_TAN, NULL},
    {"ATN", C_FUNC, F_ATN, NULL},
    {"LOG", C_FUNC, F_LOG, NULL},
    {"EXP", C_FUNC, F_EXP, NULL},
    {"RND", C_FUNC, F_RND, NULL},
    {"LEN", C_FUNC, F_LEN, NULL},
    {"VAL", C_FUNC, F_VAL, NULL},
    {"ASC", C_FUNC, F_ASC, NULL},
    {"CHR$", C_FUNC, F_CHR, NULL},
    {"STR$", C_FUNC, F_STR, NULL},
    {"LEFT$", C_FUNC, F_LEFT, NULL},
    {"RIGHT$", C_FUNC, F_RIGHT, NULL},
    {"MID$", C_FUNC, F_MID, NULL},
    {"INSTR", C_FUNC, F_INSTR, NULL},
    {"STRING$", C_FUNC, F_STRING, NULL},
    {"SPACE$", C_FUNC, F_SPACE, NULL},
    {"UPPER$", C_FUNC, F_UPPER, NULL},
    {"LOWER$", C_FUNC, F_LOWER, NULL},
    {"TRIM$", C_FUNC, F_TRIM, NULL},
    {"HEX$", C_FUNC, F_HEX, NULL},
    {"OCT$", C_FUNC, F_OCT, NULL},
    {"BIN$", C_FUNC, F_BIN, NULL},
    {"NOT", C_FUNC, F_NOT, NULL},
    {"EOF", C_FUNC, F_EOF, NULL},
    {"LOF", C_FUNC, F_LOF, NULL},
    {"LOC", C_FUNC, F_LOC, NULL},
    {"PI", C_FUNC, F_PI, NULL},
    {"PEEK", C_FUNC, F_PEEK, NULL},
    {"MAX", C_FUNC, F_MAX, NULL},
    {"MIN", C_FUNC, F_MIN, NULL},
    {"DEG", C_FUNC, F_DEG, NULL},
    {"RAD", C_FUNC, F_RAD, NULL},
    {"ACOS", C_FUNC, F_ACOS, NULL},
    {"ASIN", C_FUNC, F_ASIN, NULL},
    {"ATAN2", C_FUNC, F_ATAN2, NULL},
    {"DATE$", C_FUNC, F_DATE, NULL},
    {"TIME$", C_FUNC, F_TIME, NULL},
    {"TAB", C_FUNC, F_TAB, NULL},
    {"SPC", C_FUNC, F_SPC, NULL},
    {"FORMAT$", C_FUNC, F_FORMAT, NULL},
    {"SCHANGE$", C_FUNC, F_SCHANGE, NULL},
    {"PIN", C_FUNC, F_GPIO_PIN, NULL},
    {"ADC", C_FUNC, F_ADC, NULL},
    {"PULSIN", C_FUNC, F_PULSIN, NULL},
    {"TOUCH", C_FUNC, F_TOUCH, NULL},
    {"RGB", C_FUNC, F_RGB, NULL},
    {"PIXEL", C_FUNC, F_POINT, NULL},
    {"EVAL$", C_FUNC, F_EVAL, NULL},
    {"BASE$", C_FUNC, F_BASE, NULL},
    {"FIX", C_FUNC, F_FIX, NULL},
    {"CHOICE", C_FUNC, F_CHOICE, NULL},
    {"BOUND", C_FUNC, F_BOUND, NULL},
    // New commands
    {"CHAIN", C_CMD, C_CHAIN, NULL},
    {"MERGE", C_CMD, C_MERGE, NULL},
    {"EXECUTE", C_CMD, C_EXECUTE, NULL},
    {"DELETE", C_CMD, C_DELETE, NULL},
    {"FLUSH", C_CMD, C_FLUSH, NULL},
    {"BACKLIGHT", C_CMD, C_BACKLIGHT, NULL},
    {"SETTICK", C_CMD, C_SETTICK, NULL},
    {"WATCHDOG", C_CMD, C_WATCHDOG, NULL},
    {"CPU", C_CMD, C_CPU, NULL},
    {"MEMORY", C_CMD, C_MEMORY, NULL},
    {"LOCAL", C_CMD, C_LOCAL, NULL},
    {"STATIC", C_CMD, C_STATIC, NULL},
    // New functions
    {"INKEY$", C_FUNC, F_INKEY, NULL},
    {"KEYDOWN", C_FUNC, F_KEYDOWN, NULL},
    {"VERSION", C_FUNC, F_VERSION, NULL},
    {"MM.HRES", C_FUNC, F_MM_HRES, NULL},
    {"MM.VRES", C_FUNC, F_MM_VRES, NULL},
    {"MM.WIDTH", C_FUNC, F_MM_WIDTH, NULL},
    {"MM.HEIGHT", C_FUNC, F_MM_HEIGHT, NULL},
    {"MM.HPOS", C_FUNC, F_MM_HPOS, NULL},
    {"MM.VPOS", C_FUNC, F_MM_VPOS, NULL},
    {"MM.DEVICE$", C_FUNC, F_MM_DEVICE, NULL},
    {"MM.FONTWIDTH", C_FUNC, F_MM_FONTWIDTH, NULL},
    {"MM.FONTHEIGHT", C_FUNC, F_MM_FONTHEIGHT, NULL},
    {"MM.ERRNO", C_FUNC, F_MM_ERRNO, NULL},
    {"MM.ERRMSG$", C_FUNC, F_MM_ERRMSG, NULL},
    {"MM.FLAGS", C_FUNC, F_MM_FLAGS, NULL},
    {"MM.DISPLAY", C_FUNC, F_MM_DISPLAY, NULL},
    {"MM.SUPPLY", C_FUNC, F_MM_SUPPLY, NULL},
    {"MM.INFO", C_FUNC, F_MM_INFO, NULL},
    {"CWD$", C_FUNC, F_CWD, NULL},
    {"DIR$", C_FUNC, F_DIR, NULL},
    {"FIELD$", C_FUNC, F_FIELD, NULL},
    {"", 0, 0, NULL}
};

// Initialize MMBasic
void MMBasic_Init(void) {
    // Allocate program memory
    progmem = (char *)malloc(HEAP_MEMORY_SIZE);
    if (progmem == NULL) {
        HAL_Display_Println("Error: Cannot allocate memory");
        while(1);
    }
    
    // Allocate variable table
    vartbl = (struct s_vartbl *)malloc(MAXVARS * sizeof(struct s_vartbl));
    if (vartbl == NULL) {
        HAL_Display_Println("Error: Cannot allocate variable table");
        while(1);
    }
    
    // Allocate const flags
    varIsConst = (bool *)malloc(MAXVARS * sizeof(bool));
    if (varIsConst == NULL) {
        HAL_Display_Println("Error: Cannot allocate const flags");
        while(1);
    }
    
    // Reset MMBasic
    MMBasic_Reset();
    
    // Display sign-on message
    HAL_Display_Print(MES_SIGNON);
    HAL_Display_Println("Ready.");
}

// Reset MMBasic
void MMBasic_Reset(void) {
    progsize = 0;
    progptr = progmem;
    linecnt = 0;
    varcnt = 0;
    subfunct = 0;
    forstackptr = 0;
    gosubstackptr = 0;
    whilestackptr = 0;
    dostackptr = 0;
    strpoolindex = 0;
    MMAbort = 0;
    
    // Close any open files
    for (int i = 1; i <= MAXOPENFILES; i++) {
        if (fileTable[i].inUse) {
            fileTable[i].file.close();
            fileTable[i].inUse = false;
        }
    }
    
    // Clear program memory
    memset(progmem, 0, HEAP_MEMORY_SIZE);
    
    // Clear variable table
    memset(vartbl, 0, MAXVARS * sizeof(struct s_vartbl));
    
    // Clear subroutine table
    memset(subfun, 0, MAXSUBFUN * sizeof(struct s_subfun));
    
    // Clear const flags
    memset(varIsConst, 0, MAXVARS * sizeof(bool));
    
    // Reset DATA pointer
    dataLineIdx = 0;
    dataOffset = 0;
    
    // Clear line pointers
    memset(lines, 0, sizeof(lines));
    memset(lineNumbers, 0, sizeof(lineNumbers));
}

// Get variable type from name suffix
// $ = string, % = integer, ! = float, no suffix = integer
char MMBasic_GetVarType(char *name) {
    int len = strlen(name);
    if (len == 0) return T_INT;
    if (name[len - 1] == '$') return T_STR;
    if (name[len - 1] == '!') return T_FLOAT;
    if (name[len - 1] == '%') return T_INT;
    return T_INT;
}

// Get next token from line
char *MMBasic_GetToken(char *line, char *token) {
    int i = 0;
    
    // Skip whitespace
    while (*line == ' ' || *line == '\t') line++;
    
    // Check for end of line
    if (*line == '\0' || *line == '\n' || *line == '\r') {
        token[0] = '\0';
        return line;
    }
    
    // Check for string literal
    if (*line == '"') {
        line++;
        while (*line != '"' && *line != '\0' && i < STRINGSIZE - 1) {
            token[i++] = *line++;
        }
        if (*line == '"') line++;
        token[i] = '\0';
        return line;
    }
    
    // Check for number
    if ((*line >= '0' && *line <= '9') || *line == '.') {
        while ((*line >= '0' && *line <= '9') || *line == '.' || *line == 'E' || *line == 'e') {
            if (i < STRINGSIZE - 1) token[i++] = *line++;
        }
        token[i] = '\0';
        return line;
    }
    
    // Check for operator
    if (strchr("+-*/=<>^&|~(),;#", *line)) {
        token[0] = *line++;
        token[1] = '\0';
        
        // Check for two-character operators
        if (*line != '\0') {
            if ((token[0] == '<' && *line == '=') || 
                (token[0] == '>' && *line == '=') ||
                (token[0] == '<' && *line == '>')) {
                token[1] = *line++;
                token[2] = '\0';
            }
        }
        
        return line;
    }
    
    // Check for variable or keyword (including $/%/! for type suffixes)
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
           *line == '%' || *line == '!') {
        if (i < STRINGSIZE - 1) token[i++] = *line++;
    }
    token[i] = '\0';
    
    // Convert to uppercase
    for (int j = 0; token[j]; j++) {
        if (token[j] >= 'a' && token[j] <= 'z') {
            token[j] = token[j] - 'a' + 'A';
        }
    }
    
    return line;
}

// Find variable by name, returns index or -1 if not found
int MMBasic_FindVariable(char *name) {
    for (int i = 0; i < varcnt; i++) {
        if (strcmp(vartbl[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Create a new variable, returns index
int MMBasic_CreateVariable(char *name, char type) {
    if (varcnt >= MAXVARS) {
        MMBasic_Error(ERR_OUT_MEMORY, "Too many variables");
        return -1;
    }
    
    // Check if variable already exists
    int idx = MMBasic_FindVariable(name);
    if (idx >= 0) {
        // Variable exists, update type if needed
        return idx;
    }
    
    // Create new variable
    idx = varcnt++;
    memset(&vartbl[idx], 0, sizeof(struct s_vartbl));
    strncpy(vartbl[idx].name, name, MAXVARLEN);
    vartbl[idx].type = type;
    
    // Allocate string memory if string type
    if (type == T_STR) {
        vartbl[idx].val.sval = (char *)malloc(STRINGSIZE);
        if (vartbl[idx].val.sval == NULL) {
            MMBasic_Error(ERR_OUT_MEMORY, "Cannot allocate string");
            return -1;
        }
        vartbl[idx].val.sval[0] = '\0';
    }
    
    return idx;
}

// Set variable value
void MMBasic_SetVariable(int index, int ival, float fval, char *sval) {
    if (index < 0 || index >= varcnt) {
        MMBasic_Error(ERR_BOUNDS, "Invalid variable index");
        return;
    }
    
    switch (vartbl[index].type) {
        case T_INT:
            vartbl[index].val.ival = ival;
            break;
        case T_FLOAT:
            vartbl[index].val.fval = fval;
            break;
        case T_STR:
            if (sval != NULL) {
                strncpy(vartbl[index].val.sval, sval, STRINGSIZE - 1);
                vartbl[index].val.sval[STRINGSIZE - 1] = '\0';
            }
            break;
    }
}

// Get variable integer value
int MMBasic_GetVariableInt(int index) {
    if (index < 0 || index >= varcnt) {
        MMBasic_Error(ERR_BOUNDS, "Invalid variable index");
        return 0;
    }
    
    switch (vartbl[index].type) {
        case T_INT:
            return vartbl[index].val.ival;
        case T_FLOAT:
            return (int)vartbl[index].val.fval;
        case T_STR:
            return atoi(vartbl[index].val.sval);
        default:
            return 0;
    }
}

// Get variable float value
float MMBasic_GetVariableFloat(int index) {
    if (index < 0 || index >= varcnt) {
        MMBasic_Error(ERR_BOUNDS, "Invalid variable index");
        return 0.0;
    }
    
    switch (vartbl[index].type) {
        case T_INT:
            return (float)vartbl[index].val.ival;
        case T_FLOAT:
            return vartbl[index].val.fval;
        case T_STR:
            return atof(vartbl[index].val.sval);
        default:
            return 0.0;
    }
}

// Get variable string value
char *MMBasic_GetVariableString(int index) {
    if (index < 0 || index >= varcnt) {
        MMBasic_Error(ERR_BOUNDS, "Invalid variable index");
        return "";
    }
    
    if (vartbl[index].type == T_STR) {
        return vartbl[index].val.sval;
    }
    
    // Convert numeric to string
    char *str = MMBasic_GetTempString();
    if (vartbl[index].type == T_INT) {
        sprintf(str, "%d", vartbl[index].val.ival);
    } else if (vartbl[index].type == T_FLOAT) {
        sprintf(str, "%g", vartbl[index].val.fval);
    } else {
        str[0] = '\0';
    }
    return str;
}

// Tokenise a line
int MMBasic_Tokenise(char *source, char *dest) {
    char token[STRINGSIZE];
    char *src = source;
    char *dst = dest;
    int len = 0;
    
    while (*src) {
        src = MMBasic_GetToken(src, token);
        
        if (token[0] == '\0') break;
        
        // Check if token is a keyword
        for (int i = 0; commandtbl[i].name[0] != '\0'; i++) {
            if (strcmp(token, commandtbl[i].name) == 0) {
                *dst++ = commandtbl[i].token;
                len++;
                goto next_token;
            }
        }
        
        // Copy token as-is
        strcpy(dst, token);
        dst += strlen(token);
        len += strlen(token);
        
        next_token:
        ;
    }
    
    *dst = '\0';
    return len;
}

// Execute a single statement (no colon splitting)
static void MMBasic_ExecuteDispatch(char *line) {
    char token[STRINGSIZE];
    int cmd;
    
    currentLine = line;
    
    // Skip whitespace
    while (*line == ' ' || *line == '\t') line++;
    
    // Check for empty sub-statement
    if (*line == '\0') return;
    
    // Get command token
    char *nextPos = MMBasic_GetToken(line, token);
    
    if (token[0] == '\0') return;
    
    // Find command
    cmd = MMBasic_GetCommand(token);
    
    // Save current position for commands that need it
    currentLine = nextPos;
    
    switch (cmd) {
        case C_PRINT:
            MMBasic_CmdPrint();
            break;
        case C_INPUT:
            MMBasic_CmdInput();
            break;
        case C_IF:
            MMBasic_CmdIf();
            break;
        case C_ELSE:
            MMBasic_CmdElse();
            break;
        case C_ENDIF:
            MMBasic_CmdEndif();
            break;
        case C_FOR:
            MMBasic_CmdFor();
            break;
        case C_NEXT:
            MMBasic_CmdNext();
            break;
        case C_GOTO:
            MMBasic_CmdGoto();
            break;
        case C_GOSUB:
            MMBasic_CmdGosub();
            break;
        case C_RETURN:
            MMBasic_CmdReturn();
            break;
        case C_END:
            MMBasic_CmdEnd();
            break;
        case C_LET:
            MMBasic_CmdLet();
            break;
        case C_DIM:
            MMBasic_CmdDim();
            break;
        case C_SUB:
        case C_FUNCTION:
            MMBasic_CmdSubFunDef();
            break;
        case C_CLS:
            HAL_Display_Clear();
            break;
        case C_COLOR:
            MMBasic_CmdColor();
            break;
        case C_LINE:
            MMBasic_CmdLine();
            break;
        case C_CIRCLE:
            MMBasic_CmdCircle();
            break;
        case C_RECT:
            MMBasic_CmdRect();
            break;
        case C_PIXEL:
            MMBasic_CmdPixel();
            break;
        case C_REM:
        case C_REM2:
            // Comment - do nothing
            break;
        case C_LOCATE:
            MMBasic_CmdLocate();
            break;
        case C_RUN:
            MMBasic_RunProgram();
            break;
        case C_LIST:
            MMBasic_ListProgram();
            break;
        case C_NEW:
            MMBasic_Reset();
            HAL_Display_Println("Ready.");
            break;
        case C_SAVE:
            MMBasic_CmdSave();
            break;
        case C_LOAD:
            MMBasic_CmdLoad();
            break;
        case C_FILES:
            MMBasic_CmdFiles();
            break;
        case C_OPEN:
            MMBasic_CmdOpen();
            break;
        case C_CLOSE:
            MMBasic_CmdClose();
            break;
        case C_SEEK:
            MMBasic_CmdSeek();
            break;
        case C_SWAP:
            MMBasic_CmdSwap();
            break;
        case C_INC:
            MMBasic_CmdInc();
            break;
        case C_PAUSE:
            MMBasic_CmdPause();
            break;
        case C_RANDOMIZE:
            MMBasic_CmdRandomize();
            break;
        case C_POKE:
            MMBasic_CmdPoke();
            break;
        case C_CONST:
            MMBasic_CmdConst();
            break;
        case C_DATA:
            // DATA is passive - lines are already stored, READ parses them
            break;
        case C_READ:
            MMBasic_CmdRead();
            break;
        case C_RESTORE:
            MMBasic_CmdRestore();
            break;
        case C_WHILE:
            MMBasic_CmdWhile();
            break;
        case C_WEND:
            MMBasic_CmdWend();
            break;
        case C_SELECT:
            MMBasic_CmdSelect();
            break;
        case C_CASE:
            MMBasic_CmdCase();
            break;
        case C_ENDSELECT:
            MMBasic_CmdEndSelect();
            break;
        case C_DO:
            MMBasic_CmdDo();
            break;
        case C_LOOP:
            MMBasic_CmdLoop();
            break;
        case C_CONTINUE:
            MMBasic_CmdContinue();
            break;
        case C_EXIT:
            MMBasic_CmdExit();
            break;
        case C_OPTION:
            MMBasic_CmdOption();
            break;
        case C_KILL:
            MMBasic_CmdKill();
            break;
        case C_MKDIR:
            MMBasic_CmdMkdir();
            break;
        case C_RMDIR:
            MMBasic_CmdRmdir();
            break;
        case C_CHDIR:
            MMBasic_CmdChdir();
            break;
        case C_COPY:
            MMBasic_CmdCopy();
            break;
        case C_RENAME:
            MMBasic_CmdRename();
            break;
        case C_DRIVE:
            MMBasic_CmdDrive();
            break;
        case C_SETPIN:
            MMBasic_CmdSetpin();
            break;
        case C_DIGOUT:
            MMBasic_CmdDigout();
            break;
        case C_PWMCMD:
            MMBasic_CmdPwm();
            break;
        case C_I2C:
            MMBasic_CmdI2c();
            break;
        case C_SPI:
            MMBasic_CmdSpi();
            break;
        case C_IR:
            MMBasic_CmdIr();
            break;
        case C_SERVO:
            MMBasic_CmdServo();
            break;
        case C_PORTCMD:
            MMBasic_CmdPort();
            break;
        case C_PULSE:
            MMBasic_CmdPulse();
            break;
        case C_BOX:
            MMBasic_CmdBox();
            break;
        case C_TEXT:
            MMBasic_CmdText();
            break;
        case C_TRIANGLE:
            MMBasic_CmdTriangle();
            break;
        case C_CLEAR:
            MMBasic_CmdClear();
            break;
        case C_ERASE:
            MMBasic_CmdErase();
            break;
        case C_REDIM:
            MMBasic_CmdRedim();
            break;
        case C_ERROR:
            MMBasic_CmdError();
            break;
        case C_TRACE:
            MMBasic_CmdTrace();
            break;
        case C_SORTCMD:
            MMBasic_CmdSort();
            break;
        case C_CALL:
            MMBasic_CmdCall();
            break;
        case C_ON:
            MMBasic_CmdOn();
            break;
        case C_CHAIN:
            MMBasic_CmdChain();
            break;
        case C_MERGE:
            MMBasic_CmdMerge();
            break;
        case C_EXECUTE:
            MMBasic_CmdExecute();
            break;
        case C_DELETE:
            MMBasic_CmdDelete();
            break;
        case C_FLUSH:
            MMBasic_CmdFlush();
            break;
        case C_BACKLIGHT:
            MMBasic_CmdBacklight();
            break;
        case C_SETTICK:
            MMBasic_CmdSettick();
            break;
        case C_WATCHDOG:
            MMBasic_CmdWatchdog();
            break;
        case C_CPU:
            MMBasic_CmdCpu();
            break;
        case C_MEMORY:
            MMBasic_CmdMemory();
            break;
        case C_LOCAL:
            MMBasic_CmdLocal();
            break;
        case C_STATIC:
            MMBasic_CmdStatic();
            break;
        case C_PRINT_HASH:
        case C_INPUT_HASH:
            // Handled inside CmdPrint/CmdInput via # detection
            break;
        case C_INVALID:
            // Try as assignment (variable = expression)
            MMBasic_CmdImplicitLet(line);
            break;
        default:
            HAL_Display_Println("Command not implemented");
            break;
    }
}

// Execute a line (handles colon-separated statements)
void MMBasic_Execute(char *line) {
    char token[STRINGSIZE];
    
    currentLine = line;
    
    // Skip whitespace
    while (*line == ' ' || *line == '\t') line++;
    
    // Check for empty line
    if (*line == '\0') return;
    
    // Get command token
    char *nextPos = MMBasic_GetToken(line, token);
    
    if (token[0] == '\0') return;
    
    // Check for line number (program storage)
    if (token[0] >= '0' && token[0] <= '9') {
        int linenum = atoi(token);
        MMBasic_StoreLine(linenum, nextPos);
        return;
    }
    
    // ----- Colon-splitting: split line by ':' into sub-statements -----
    #define MAX_SUBSTMTS 32
    char lineCopy[STRINGSIZE];
    strncpy(lineCopy, line, STRINGSIZE - 1);
    lineCopy[STRINGSIZE - 1] = '\0';
    
    char *stmts[MAX_SUBSTMTS];
    int stmtCount = 0;
    char *p = lineCopy;
    int inString = 0;
    int foundComment = 0;
    
    while (*p && stmtCount < MAX_SUBSTMTS && !foundComment) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;
        
        char *start = p;
        while (*p) {
            if (*p == '"') inString = !inString;
            if (*p == '\'' && !inString) {
                foundComment = 1;
                *p = '\0';
                break;
            }
            if (*p == ':' && !inString) break;
            p++;
        }
        
        int hasColon = (!foundComment && *p == ':');
        *p = '\0';
        
        // Trim trailing whitespace
        char *t = p - 1;
        while (t >= start && (*t == ' ' || *t == '\t')) { *t = '\0'; t--; }
        
        stmts[stmtCount++] = start;
        
        if (foundComment || !hasColon) break;
        p++;
    }
    
    // Detect single-line FOR (NEXT on same line)
    MMBasic_ForSingleLine = 0;
    if (stmtCount > 1) {
        char ftok[STRINGSIZE];
        char *fp = stmts[0];
        MMBasic_GetToken(fp, ftok);
        if ((ftok[0]=='F'||ftok[0]=='f') && (ftok[1]=='O'||ftok[1]=='o') &&
            (ftok[2]=='R'||ftok[2]=='r')) {
            for (int si = 1; si < stmtCount; si++) {
                char nt[STRINGSIZE];
                char *np = stmts[si];
                MMBasic_GetToken(np, nt);
                if ((nt[0]=='N'||nt[0]=='n') && (nt[1]=='E'||nt[1]=='e') &&
                    (nt[2]=='X'||nt[2]=='x') && (nt[3]=='T'||nt[3]=='t')) {
                    MMBasic_ForSingleLine = 1;
                    break;
                }
            }
        }
    }
    
    // Execute each sub-statement in sequence
    for (int si = 0; si < stmtCount; si++) {
        // If sub-statement is REM or ', skip rest of line
        char subTok[STRINGSIZE];
        MMBasic_GetToken(stmts[si], subTok);
        if (strcmp(subTok, "REM") == 0 || (subTok[0] == '\'' && subTok[1] == '\0')) {
            break;
        }
        
        MMBasic_ExecuteDispatch(stmts[si]);
        
        // If flow was redirected (GOTO, loop back, etc.), stop processing
        if (flowControlActive) break;
    }
}

// Get command from token
int MMBasic_GetCommand(char *token) {
    for (int i = 0; commandtbl[i].name[0] != '\0'; i++) {
        if (strcmp(token, commandtbl[i].name) == 0) {
            if (commandtbl[i].type == C_CMD) {
                return commandtbl[i].token;
            }
        }
    }
    return C_INVALID;
}

// Get function from token
int MMBasic_GetFunction(char *token) {
    for (int i = 0; commandtbl[i].name[0] != '\0'; i++) {
        if (strcmp(token, commandtbl[i].name) == 0) {
            if (commandtbl[i].type == C_FUNC) {
                return commandtbl[i].token;
            }
        }
    }
    return C_INVALID;
}

// ON ERROR SKIP support
int onErrorSkipCount = 0;
int onErrorSkipActive = 0;  // Set to 1 when a skip is triggered

// Error handling
void MMBasic_Error(int error, char *msg) {
    // Check if ON ERROR SKIP is active
    if (onErrorSkipCount > 0) {
        onErrorSkipActive = onErrorSkipCount;
        onErrorSkipCount = 0;  // One-shot: clear after firing
        longjmp(mark, 2);      // Signal "skip" to run loop
    }

    HAL_Display_Print("?Error: ");
    if (msg != NULL) {
        HAL_Display_Println(msg);
    } else {
        switch (error) {
            case ERR_SYNTAX:
                HAL_Display_Println("Syntax error");
                break;
            case ERR_UNKNOWN_CMD:
                HAL_Display_Println("Unknown command");
                break;
            case ERR_ARGUMENT:
                HAL_Display_Println("Invalid argument");
                break;
            case ERR_DIV_ZERO:
                HAL_Display_Println("Division by zero");
                break;
            case ERR_OVERFLOW:
                HAL_Display_Println("Overflow");
                break;
            case ERR_OUT_MEMORY:
                HAL_Display_Println("Out of memory");
                break;
            case ERR_STACK_FULL:
                HAL_Display_Println("Stack full");
                break;
            case ERR_STACK_EMPTY:
                HAL_Display_Println("Stack empty");
                break;
            case ERR_FOR_NEXT:
                HAL_Display_Println("FOR/NEXT mismatch");
                break;
            case ERR_GOSUB:
                HAL_Display_Println("GOSUB stack full");
                break;
            case ERR_RETURN:
                HAL_Display_Println("RETURN without GOSUB");
                break;
            case ERR_FILE_IO:
                HAL_Display_Println("File I/O error");
                break;
            case ERR_TYPE:
                HAL_Display_Println("Type mismatch");
                break;
            case ERR_BOUNDS:
                HAL_Display_Println("Out of bounds");
                break;
            default:
                HAL_Display_Println("Unknown error");
                break;
        }
    }
    
    // Reset stacks
    forstackptr = 0;
    gosubstackptr = 0;
    
    longjmp(mark, 1);
}

// Get temporary string from pool
char *MMBasic_GetTempString(void) {
    char *str = strpool[strpoolindex];
    strpoolindex = (strpoolindex + 1) % 16;
    str[0] = '\0';
    return str;
}

// ============================================================================
// Expression Evaluator
// ============================================================================

// Main expression evaluator
static int EvaluateExpression(char **expr, int *itype, int *ival, float *fval, char **sval) {
    return EvaluateSubExpression(expr, itype, ival, fval, sval);
}

// Handle OR, XOR
static int EvaluateSubExpression(char **expr, int *itype, int *ival, float *fval, char **sval) {
    int leftType, leftIval;
    float leftFval;
    char *leftSval;
    
    // Get left operand
    if (EvaluateTerm(expr, &leftType, &leftIval, &leftFval, &leftSval)) return 1;
    
    while (**expr) {
        // Skip whitespace
        while (**expr == ' ') (*expr)++;
        
        // Check for OR/XOR
        int op = 0;
        if (strncmp(*expr, "OR", 2) == 0 && !isalpha((*expr)[2])) {
            op = OP_OR;
            *expr += 2;
        } else if (strncmp(*expr, "XOR", 3) == 0 && !isalpha((*expr)[3])) {
            op = OP_XOR;
            *expr += 3;
        } else {
            break;
        }
        
        int rightType, rightIval;
        float rightFval;
        char *rightSval;
        
        if (EvaluateTerm(expr, &rightType, &rightIval, &rightFval, &rightSval)) return 1;
        
        // Perform operation
        int result = 0;
        if (op == OP_OR) {
            result = (leftIval || rightIval);
        } else if (op == OP_XOR) {
            result = (leftIval != rightIval);
        }
        
        leftType = T_INT;
        leftIval = result;
    }
    
    *itype = leftType;
    *ival = leftIval;
    *fval = leftFval;
    *sval = leftSval;
    return 0;
}

// Handle AND
static int EvaluateTerm(char **expr, int *itype, int *ival, float *fval, char **sval) {
    int leftType, leftIval;
    float leftFval;
    char *leftSval;
    
    // Get left operand
    if (EvaluateFactor(expr, &leftType, &leftIval, &leftFval, &leftSval)) return 1;
    
    while (**expr) {
        // Skip whitespace
        while (**expr == ' ') (*expr)++;
        
        // Check for AND
        if (strncmp(*expr, "AND", 3) == 0 && !isalpha((*expr)[3])) {
            *expr += 3;
        } else {
            break;
        }
        
        int rightType, rightIval;
        float rightFval;
        char *rightSval;
        
        if (EvaluateFactor(expr, &rightType, &rightIval, &rightFval, &rightSval)) return 1;
        
        // Perform AND operation
        leftIval = (leftIval && rightIval);
        leftType = T_INT;
    }
    
    *itype = leftType;
    *ival = leftIval;
    *fval = leftFval;
    *sval = leftSval;
    return 0;
}

// Handle comparisons and basic arithmetic
static int EvaluateFactor(char **expr, int *itype, int *ival, float *fval, char **sval) {
    int leftType, leftIval;
    float leftFval;
    char *leftSval;
    
    // Handle NOT
    if (strncmp(*expr, "NOT", 3) == 0 && !isalpha((*expr)[3])) {
        *expr += 3;
        if (EvaluateFactor(expr, &leftType, &leftIval, &leftFval, &leftSval)) return 1;
        *itype = T_INT;
        *ival = !leftIval;
        return 0;
    }
    
    // Handle unary minus
    int negate = 0;
    while (**expr == ' ') (*expr)++;
    if (**expr == '-') {
        negate = 1;
        (*expr)++;
    } else if (**expr == '+') {
        (*expr)++;
    }
    
    // Parse the primary value
    while (**expr == ' ') (*expr)++;
    
    // Check for parentheses
    if (**expr == '(') {
        (*expr)++;
        if (EvaluateExpression(expr, &leftType, &leftIval, &leftFval, &leftSval)) return 1;
        if (**expr == ')') (*expr)++;
        else {
            MMBasic_Error(ERR_SYNTAX, "Missing )");
            return 1;
        }
    }
    // Check for file number (#fnbr)
    else if (**expr == '#') {
        (*expr)++;
        while (**expr == ' ') (*expr)++;
        char numStr[16];
        int i = 0;
        while (**expr >= '0' && **expr <= '9' && i < 15) {
            numStr[i++] = **expr;
            (*expr)++;
        }
        numStr[i] = '\0';
        leftType = T_INT;
        leftIval = atoi(numStr);
        leftFval = (float)leftIval;
    }
    // Check for string literal
    else if (**expr == '"') {
        (*expr)++;
        leftSval = MMBasic_GetTempString();
        int i = 0;
        while (**expr != '"' && **expr != '\0' && i < STRINGSIZE - 1) {
            leftSval[i++] = **expr;
            (*expr)++;
        }
        leftSval[i] = '\0';
        if (**expr == '"') (*expr)++;
        leftType = T_STR;
        leftIval = 0;
        leftFval = 0.0;
    }
    // Check for number
    else if ((**expr >= '0' && **expr <= '9') || **expr == '.') {
        char numStr[64];
        int i = 0;
        int hasDot = 0;
        while ((**expr >= '0' && **expr <= '9') || **expr == '.' || **expr == 'E' || **expr == 'e') {
            if (**expr == '.') hasDot = 1;
            if (i < 63) numStr[i++] = **expr;
            (*expr)++;
        }
        numStr[i] = '\0';
        
        if (hasDot) {
            leftType = T_FLOAT;
            leftFval = atof(numStr);
            leftIval = (int)leftFval;
        } else {
            leftType = T_INT;
            leftIval = atoi(numStr);
            leftFval = (float)leftIval;
        }
        leftSval = NULL;
    }
    // Check for function or variable
    else if ((*expr[0] >= 'A' && *expr[0] <= 'Z') || (*expr[0] >= 'a' && *expr[0] <= 'z')) {
        char name[MAXVARLEN + 2]; // +2 for $ and null
        int i = 0;
        while ((**expr >= 'A' && **expr <= 'Z') || (**expr >= 'a' && **expr <= 'z') || 
               (**expr >= '0' && **expr <= '9') || **expr == '_' || **expr == '$' ||
               **expr == '%' || **expr == '!') {
            if (i < MAXVARLEN + 1) name[i++] = **expr;
            (*expr)++;
        }
        name[i] = '\0';
        
        // Convert to uppercase
        for (int j = 0; name[j]; j++) {
            if (name[j] >= 'a' && name[j] <= 'z') {
                name[j] = name[j] - 'a' + 'A';
            }
        }
        
        // Check if it's a function
        int funcToken = MMBasic_GetFunction(name);
        if (funcToken != C_INVALID) {
            if (EvaluateFunction(expr, funcToken, &leftType, &leftIval, &leftFval, &leftSval)) return 1;
            // Don't return - continue to operator handling below
        } else {
            // It's a variable
            int varIdx = MMBasic_FindVariable(name);
            if (varIdx < 0) {
                char type = MMBasic_GetVarType(name);
                varIdx = MMBasic_CreateVariable(name, type);
                if (varIdx < 0) return 1;
            }

            // Check for array access: variable(indices)
            while (**expr == ' ') (*expr)++;
            if (vartbl[varIdx].ndims > 0 && **expr == '(') {
                (*expr)++; // skip '('
                int indices[MAXDIMS] = {0};
                int nIdx = 0;
                while (nIdx < MAXDIMS) {
                    int idxType, idxIval;
                    float idxFval;
                    char *idxSval;
                    if (EvaluateExpression(expr, &idxType, &idxIval, &idxFval, &idxSval)) return 1;
                    indices[nIdx++] = idxIval;
                    while (**expr == ' ') (*expr)++;
                    if (**expr == ',') { (*expr)++; while (**expr == ' ') (*expr)++; }
                    else break;
                }
                while (**expr == ' ') (*expr)++;
                if (**expr == ')') (*expr)++;
                else { MMBasic_Error(ERR_SYNTAX, "Expected )"); return 1; }

                // Calculate flat index
                int flatIdx = 0;
                for (int d = 0; d < nIdx; d++) {
                    int stride = 1;
                    for (int s = d + 1; s < vartbl[varIdx].ndims; s++) {
                        stride *= (vartbl[varIdx].dims[s] + 1);
                    }
                    flatIdx += indices[d] * stride;
                }

                // Bounds check
                int totalSize = 1;
                for (int d = 0; d < vartbl[varIdx].ndims; d++) totalSize *= (vartbl[varIdx].dims[d] + 1);
                if (flatIdx < 0 || flatIdx >= totalSize) {
                    MMBasic_Error(ERR_BOUNDS, "Array index out of bounds");
                    return 1;
                }

                leftType = vartbl[varIdx].type;
                if (leftType == T_STR) {
                    leftSval = ((char **)vartbl[varIdx].arr)[flatIdx];
                    leftIval = 0;
                    leftFval = 0.0;
                } else if (leftType == T_FLOAT) {
                    leftFval = ((float *)vartbl[varIdx].arr)[flatIdx];
                    leftIval = (int)leftFval;
                    leftSval = NULL;
                } else {
                    leftIval = vartbl[varIdx].arr[flatIdx];
                    leftFval = (float)leftIval;
                    leftSval = NULL;
                }
            } else {
                leftType = vartbl[varIdx].type;
                leftIval = vartbl[varIdx].val.ival;
                leftFval = vartbl[varIdx].val.fval;
                leftSval = vartbl[varIdx].val.sval;
            }
        }
    }
    else {
        MMBasic_Error(ERR_SYNTAX, "Expected value");
        return 1;
    }
    
    // Apply unary minus
    if (negate) {
        if (leftType == T_INT) leftIval = -leftIval;
        else if (leftType == T_FLOAT) leftFval = -leftFval;
    }
    
    // Check for comparison operators
    while (**expr == ' ') (*expr)++;
    
    int compOp = 0;
    if (**expr == '=' && (*expr)[1] != '=') {
        compOp = OP_EQ;
        (*expr)++;
    } else if (**expr == '<') {
        if ((*expr)[1] == '>') {
            compOp = OP_NE;
            *expr += 2;
        } else if ((*expr)[1] == '=') {
            compOp = OP_LE;
            *expr += 2;
        } else {
            compOp = OP_LT;
            (*expr)++;
        }
    } else if (**expr == '>') {
        if ((*expr)[1] == '=') {
            compOp = OP_GE;
            *expr += 2;
        } else {
            compOp = OP_GT;
            (*expr)++;
        }
    }
    
    if (compOp) {
        int rightType, rightIval;
        float rightFval;
        char *rightSval;
        
        if (EvaluateFactor(expr, &rightType, &rightIval, &rightFval, &rightSval)) return 1;
        
        // Perform comparison
        int result = 0;
        if (leftType == T_STR && rightType == T_STR) {
            int cmp = strcmp(leftSval, rightSval);
            switch (compOp) {
                case OP_EQ: result = (cmp == 0); break;
                case OP_NE: result = (cmp != 0); break;
                case OP_LT: result = (cmp < 0); break;
                case OP_GT: result = (cmp > 0); break;
                case OP_LE: result = (cmp <= 0); break;
                case OP_GE: result = (cmp >= 0); break;
            }
        } else {
            float lval = (leftType == T_INT) ? (float)leftIval : leftFval;
            float rval = (rightType == T_INT) ? (float)rightIval : rightFval;
            switch (compOp) {
                case OP_EQ: result = (lval == rval); break;
                case OP_NE: result = (lval != rval); break;
                case OP_LT: result = (lval < rval); break;
                case OP_GT: result = (lval > rval); break;
                case OP_LE: result = (lval <= rval); break;
                case OP_GE: result = (lval >= rval); break;
            }
        }
        
        leftType = T_INT;
        leftIval = result;
        leftFval = (float)result;
        leftSval = NULL;
    }
    
    // Handle arithmetic operators (+, -, *, /, MOD, ^)
    while (**expr == ' ') (*expr)++;
    
    while (**expr == '+' || **expr == '-' || **expr == '*' || **expr == '/' || 
           **expr == '^' || strncmp(*expr, "MOD", 3) == 0) {
        
        int op;
        if (**expr == '+') { op = OP_PLUS; (*expr)++; }
        else if (**expr == '-') { op = OP_MINUS; (*expr)++; }
        else if (**expr == '*') { op = OP_MULTIPLY; (*expr)++; }
        else if (**expr == '/') { op = OP_DIVIDE; (*expr)++; }
        else if (**expr == '^') { op = OP_POWER; (*expr)++; }
        else if (strncmp(*expr, "MOD", 3) == 0 && !isalpha((*expr)[3])) { op = OP_MOD; *expr += 3; }
        else break;
        
        int rightType, rightIval;
        float rightFval;
        char *rightSval;
        
        // Handle unary minus for right operand
        while (**expr == ' ') (*expr)++;
        int rightNegate = 0;
        if (**expr == '-') {
            rightNegate = 1;
            (*expr)++;
        }
        
        if (EvaluateFactor(expr, &rightType, &rightIval, &rightFval, &rightSval)) return 1;
        
        if (rightNegate) {
            if (rightType == T_INT) rightIval = -rightIval;
            else if (rightType == T_FLOAT) rightFval = -rightFval;
        }
        
        // String concatenation with +
        if (op == OP_PLUS && leftType == T_STR && rightType == T_STR) {
            char *result = MMBasic_GetTempString();
            strncpy(result, leftSval, STRINGSIZE - 1);
            strncat(result, rightSval, STRINGSIZE - strlen(result) - 1);
            leftSval = result;
            continue;
        }
        
        // Numeric operations
        float lval = (leftType == T_INT) ? (float)leftIval : leftFval;
        float rval = (rightType == T_INT) ? (float)rightIval : rightFval;
        
        switch (op) {
            case OP_PLUS:
                lval += rval;
                break;
            case OP_MINUS:
                lval -= rval;
                break;
            case OP_MULTIPLY:
                lval *= rval;
                break;
            case OP_DIVIDE:
                if (rval == 0) {
                    MMBasic_Error(ERR_DIV_ZERO, NULL);
                    return 1;
                }
                lval /= rval;
                break;
            case OP_MOD:
                if (rval == 0) {
                    MMBasic_Error(ERR_DIV_ZERO, NULL);
                    return 1;
                }
                lval = fmod(lval, rval);
                break;
            case OP_POWER:
                lval = pow(lval, rval);
                break;
        }
        
        // Determine result type
        if (leftType == T_INT && rightType == T_INT && op != OP_DIVIDE && op != OP_POWER) {
            leftType = T_INT;
            leftIval = (int)lval;
            leftFval = lval;
        } else {
            leftType = T_FLOAT;
            leftFval = lval;
            leftIval = (int)lval;
        }
        leftSval = NULL;
        
        while (**expr == ' ') (*expr)++;
    }
    
    *itype = leftType;
    *ival = leftIval;
    *fval = leftFval;
    *sval = leftSval;
    return 0;
}

// Evaluate a function call
static int EvaluateFunction(char **expr, int funcToken, int *itype, int *ival, float *fval, char **sval) {
    // Special handling for BOUND() - needs variable name, not value
    if (funcToken == F_BOUND) {
        // Expect opening parenthesis
        while (**expr == ' ') (*expr)++;
        if (**expr != '(') { MMBasic_Error(ERR_SYNTAX, "Expected ("); return 1; }
        (*expr)++;

        // Parse variable name (including type suffix !, %, $)
        while (**expr == ' ') (*expr)++;
        char name[MAXVARLEN + 2];
        int i = 0;
        while ((**expr >= 'A' && **expr <= 'Z') || (**expr >= 'a' && **expr <= 'z') ||
               (**expr >= '0' && **expr <= '9') || **expr == '_' || **expr == '$' ||
               **expr == '!' || **expr == '%') {
            if (i < MAXVARLEN + 1) name[i++] = **expr;
            (*expr)++;
        }
        name[i] = '\0';
        for (int j = 0; name[j]; j++) {
            if (name[j] >= 'a' && name[j] <= 'z') name[j] = name[j] - 'a' + 'A';
        }

        // Skip optional ()
        while (**expr == ' ') (*expr)++;
        if (**expr == '(') { (*expr)++; while (**expr == ' ') (*expr)++; if (**expr == ')') (*expr)++; }

        // Expect comma
        while (**expr == ' ') (*expr)++;
        if (**expr != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return 1; }
        (*expr)++;

        // Parse dimension number
        int dimType, dimIval;
        float dimFval;
        char *dimSval;
        if (EvaluateExpression(expr, &dimType, &dimIval, &dimFval, &dimSval)) return 1;

        // Expect closing parenthesis
        while (**expr == ' ') (*expr)++;
        if (**expr != ')') { MMBasic_Error(ERR_SYNTAX, "Expected )"); return 1; }
        (*expr)++;

        // Find variable and return bound
        int varIdx = MMBasic_FindVariable(name);
        if (varIdx < 0 || vartbl[varIdx].ndims == 0) {
            *itype = T_INT; *ival = 0; *fval = 0;
        } else {
            int d = dimIval - 1; // BOUND uses 1-based dimension
            if (d < 0 || d >= vartbl[varIdx].ndims) {
                *itype = T_INT; *ival = 0; *fval = 0;
            } else {
                *itype = T_INT;
                *ival = vartbl[varIdx].dims[d];
                *fval = (float)*ival;
            }
        }
        return 0;
    }

    // Functions that don't require parentheses
    int noArg = (funcToken == F_RND || funcToken == F_PI || funcToken == F_DATE || funcToken == F_TIME);

    // Skip optional opening parenthesis
    int hasParen = 0;
    if (**expr == '(') {
        hasParen = 1;
        (*expr)++;
    } else if (!noArg) {
        MMBasic_Error(ERR_SYNTAX, "Expected (");
        return 1;
    }

    // Get argument(s)
    int arg1Type = T_INT, arg1Ival = 0;
    float arg1Fval = 0;
    char *arg1Sval = NULL;
    int arg2Type = T_INT, arg2Ival = 0;
    float arg2Fval = 0;
    char *arg2Sval = NULL;
    int hasArg2 = 0;
    int arg3Type = T_INT, arg3Ival = 0;
    float arg3Fval = 0;
    char *arg3Sval = NULL;
    int hasArg3 = 0;
    
    if (!noArg || hasParen) {
        // Check for empty parens: RND() or PI()
        while (**expr == ' ') (*expr)++;
        if (**expr == ')') {
            (*expr)++;
            hasParen = 0; // Consumed, don't expect closing paren
        } else {
            if (EvaluateExpression(expr, &arg1Type, &arg1Ival, &arg1Fval, &arg1Sval)) return 1;
            
            if (**expr == ',') {
                (*expr)++;
                hasArg2 = 1;
                if (EvaluateExpression(expr, &arg2Type, &arg2Ival, &arg2Fval, &arg2Sval)) return 1;
            }
            
            if (**expr == ',') {
                (*expr)++;
                hasArg3 = 1;
                if (EvaluateExpression(expr, &arg3Type, &arg3Ival, &arg3Fval, &arg3Sval)) return 1;
            }
        }
    }
    
    // Expect closing parenthesis (if we opened one)
    if (hasParen) {
        if (**expr == ')') (*expr)++;
        else {
            MMBasic_Error(ERR_SYNTAX, "Expected )");
            return 1;
        }
    }
    
    // Get numeric values
    float numVal = (arg1Type == T_INT) ? (float)arg1Ival : arg1Fval;
    float numVal2 = (arg2Type == T_INT) ? (float)arg2Ival : arg2Fval;
    
    // Execute function
    switch (funcToken) {
        case F_ABS:
            *itype = (arg1Type == T_INT) ? T_INT : T_FLOAT;
            *ival = abs(arg1Ival);
            *fval = fabs(numVal);
            break;
        case F_INT:
            *itype = T_INT;
            *ival = (int)numVal;
            *fval = (float)*ival;
            break;
        case F_SGN:
            *itype = T_INT;
            if (numVal > 0) *ival = 1;
            else if (numVal < 0) *ival = -1;
            else *ival = 0;
            *fval = (float)*ival;
            break;
        case F_SQR:
            *itype = T_FLOAT;
            *fval = sqrt(numVal);
            *ival = (int)*fval;
            break;
        case F_SIN:
            *itype = T_FLOAT;
            *fval = sin(numVal);
            *ival = (int)*fval;
            break;
        case F_COS:
            *itype = T_FLOAT;
            *fval = cos(numVal);
            *ival = (int)*fval;
            break;
        case F_TAN:
            *itype = T_FLOAT;
            *fval = tan(numVal);
            *ival = (int)*fval;
            break;
        case F_ATN:
            *itype = T_FLOAT;
            *fval = atan(numVal);
            *ival = (int)*fval;
            break;
        case F_LOG:
            *itype = T_FLOAT;
            *fval = log(numVal);
            *ival = (int)*fval;
            break;
        case F_EXP:
            *itype = T_FLOAT;
            *fval = exp(numVal);
            *ival = (int)*fval;
            break;
        case F_RND:
            *itype = T_FLOAT;
            *fval = (float)rand() / ((float)RAND_MAX + 1.0f);
            *ival = (int)*fval;
            break;
        case F_LEN:
            *itype = T_INT;
            if (arg1Type == T_STR && arg1Sval != NULL) {
                *ival = strlen(arg1Sval);
            } else {
                *ival = 0;
            }
            *fval = (float)*ival;
            break;
        case F_VAL:
            *itype = T_FLOAT;
            if (arg1Type == T_STR && arg1Sval != NULL) {
                *fval = atof(arg1Sval);
            } else {
                *fval = 0.0;
            }
            *ival = (int)*fval;
            break;
        case F_ASC:
            *itype = T_INT;
            if (arg1Type == T_STR && arg1Sval != NULL && arg1Sval[0] != '\0') {
                *ival = arg1Sval[0];
            } else {
                *ival = 0;
            }
            *fval = (float)*ival;
            break;
        case F_CHR:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            (*sval)[0] = (char)arg1Ival;
            (*sval)[1] = '\0';
            break;
        case F_STR:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            sprintf(*sval, "%g", numVal);
            break;
        case F_LEFT:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            if (arg1Type == T_STR && arg1Sval != NULL) {
                int len = hasArg2 ? arg2Ival : 0;
                if (len > strlen(arg1Sval)) len = strlen(arg1Sval);
                strncpy(*sval, arg1Sval, len);
                (*sval)[len] = '\0';
            }
            break;
        case F_RIGHT:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            if (arg1Type == T_STR && arg1Sval != NULL) {
                int len = hasArg2 ? arg2Ival : 0;
                int slen = strlen(arg1Sval);
                if (len > slen) len = slen;
                strcpy(*sval, arg1Sval + slen - len);
            }
            break;
        case F_MID:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            if (arg1Type == T_STR && arg1Sval != NULL) {
                int start = arg2Ival - 1; // BASIC is 1-based
                int len = hasArg3 ? arg3Ival : strlen(arg1Sval);
                int slen = strlen(arg1Sval);
                if (start < 0) start = 0;
                if (start >= slen) {
                    (*sval)[0] = '\0';
                } else {
                    if (start + len > slen) len = slen - start;
                    strncpy(*sval, arg1Sval + start, len);
                    (*sval)[len] = '\0';
                }
            }
            break;
        case F_INSTR:
            *itype = T_INT;
            *ival = 0;
            if (hasArg3) {
                // 3-parameter form: INSTR(haystack$, needle$, start)
                if (arg1Type == T_STR && arg2Type == T_STR) {
                    int startPos = arg3Ival; // 1-based BASIC start position
                    int slen = strlen(arg1Sval);
                    if (startPos < 1) startPos = 1;
                    if (startPos <= slen) {
                        char *pos = strstr(arg1Sval + startPos - 1, arg2Sval);
                        if (pos != NULL) {
                            *ival = (int)(pos - arg1Sval) + 1; // 1-based
                        }
                    }
                }
            } else {
                // 2-parameter form: INSTR(haystack$, needle$)
                if (arg1Type == T_STR && arg2Type == T_STR) {
                    char *pos = strstr(arg1Sval, arg2Sval);
                    if (pos != NULL) {
                        *ival = (int)(pos - arg1Sval) + 1; // 1-based
                    }
                }
            }
            *fval = (float)*ival;
            break;
        case F_UPPER:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            if (arg1Type == T_STR && arg1Sval != NULL) {
                strcpy(*sval, arg1Sval);
                for (int i = 0; (*sval)[i]; i++) {
                    if ((*sval)[i] >= 'a' && (*sval)[i] <= 'z') {
                        (*sval)[i] -= 32;
                    }
                }
            }
            break;
        case F_LOWER:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            if (arg1Type == T_STR && arg1Sval != NULL) {
                strcpy(*sval, arg1Sval);
                for (int i = 0; (*sval)[i]; i++) {
                    if ((*sval)[i] >= 'A' && (*sval)[i] <= 'Z') {
                        (*sval)[i] += 32;
                    }
                }
            }
            break;
        case F_TRIM:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            if (arg1Type == T_STR && arg1Sval != NULL) {
                // Trim leading spaces
                while (*arg1Sval == ' ') arg1Sval++;
                strcpy(*sval, arg1Sval);
                // Trim trailing spaces
                int len = strlen(*sval);
                while (len > 0 && (*sval)[len - 1] == ' ') {
                    (*sval)[--len] = '\0';
                }
            }
            break;
        case F_HEX:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            sprintf(*sval, "%X", arg1Ival);
            break;
        case F_OCT:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            sprintf(*sval, "%o", arg1Ival);
            break;
        case F_BIN:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            // Convert to binary string
            {
                int val = arg1Ival;
                int i = 0;
                if (val == 0) {
                    (*sval)[i++] = '0';
                } else {
                    while (val > 0 && i < 32) {
                        (*sval)[i++] = (val & 1) ? '1' : '0';
                        val >>= 1;
                    }
                }
                (*sval)[i] = '\0';
                // Reverse string
                for (int j = 0; j < i / 2; j++) {
                    char tmp = (*sval)[j];
                    (*sval)[j] = (*sval)[i - 1 - j];
                    (*sval)[i - 1 - j] = tmp;
                }
            }
            break;
        case F_STRING:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            {
                int len = arg1Ival;
                char ch = (arg2Type == T_STR && arg2Sval != NULL) ? arg2Sval[0] : ' ';
                if (len >= STRINGSIZE) len = STRINGSIZE - 1;
                memset(*sval, ch, len);
                (*sval)[len] = '\0';
            }
            break;
        case F_SPACE:
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            {
                int len = arg1Ival;
                if (len >= STRINGSIZE) len = STRINGSIZE - 1;
                memset(*sval, ' ', len);
                (*sval)[len] = '\0';
            }
            break;
        case F_EOF: {
            int f = arg1Ival;
            if (f < 1 || f > MAXOPENFILES || !fileTable[f].inUse) {
                MMBasic_Error(ERR_FILE_IO, "File not open");
                return 1;
            }
            *itype = T_INT;
            *ival = !fileTable[f].file.available();
            *fval = *ival;
            break;
        }
        case F_LOF: {
            int f = arg1Ival;
            if (f < 1 || f > MAXOPENFILES || !fileTable[f].inUse) {
                MMBasic_Error(ERR_FILE_IO, "File not open");
                return 1;
            }
            *itype = T_INT;
            *ival = fileTable[f].file.size();
            *fval = *ival;
            break;
        }
        case F_LOC: {
            int f = arg1Ival;
            if (f < 1 || f > MAXOPENFILES || !fileTable[f].inUse) {
                MMBasic_Error(ERR_FILE_IO, "File not open");
                return 1;
            }
            *itype = T_INT;
            *ival = fileTable[f].file.position();
            *fval = *ival;
            break;
        }
        case F_PI:
            *itype = T_FLOAT;
            *fval = 3.14159265358979323846;
            *ival = 3;
            break;
        case F_PEEK: {
            unsigned char *addr = (unsigned char *)arg1Ival;
            *itype = T_INT;
            *ival = *addr;
            *fval = *ival;
            break;
        }
        case F_MAX:
            *itype = T_FLOAT;
            *fval = (numVal > numVal2) ? numVal : numVal2;
            *ival = (int)*fval;
            break;
        case F_MIN:
            *itype = T_FLOAT;
            *fval = (numVal < numVal2) ? numVal : numVal2;
            *ival = (int)*fval;
            break;
        case F_DEG:
            *itype = T_FLOAT;
            *fval = numVal * 57.2957795131f; // 180/PI
            *ival = (int)*fval;
            break;
        case F_RAD:
            *itype = T_FLOAT;
            *fval = numVal * 0.01745329252f; // PI/180
            *ival = (int)*fval;
            break;
        case F_ACOS:
            *itype = T_FLOAT;
            *fval = acos(numVal);
            *ival = (int)*fval;
            break;
        case F_ASIN:
            *itype = T_FLOAT;
            *fval = asin(numVal);
            *ival = (int)*fval;
            break;
        case F_ATAN2:
            *itype = T_FLOAT;
            *fval = atan2(numVal, numVal2);
            *ival = (int)*fval;
            break;
        case F_DATE: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            unsigned long now = millis();
            // Simple date: days since epoch, not calendar date
            // Return system uptime-based representation
            int days = now / 86400000;
            int years = 1970 + days / 365;
            int rem = days % 365;
            int months = rem / 30 + 1;
            if (months > 12) months = 12;
            int mdays = rem % 30 + 1;
            sprintf(*sval, "%02d-%02d-%04d", mdays, months, years);
            break;
        }
        case F_TIME: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            unsigned long now = millis();
            int totalSec = (now / 1000) % 86400;
            int h = totalSec / 3600;
            int m = (totalSec % 3600) / 60;
            int s = totalSec % 60;
            sprintf(*sval, "%02d:%02d:%02d", h, m, s);
            break;
        }
        case F_TAB: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            int target = arg1Ival - 1;
            if (target < 0) target = 0;
            int spaces = target - MMCharPos + 1;
            if (spaces <= 0) spaces = 1;
            if (spaces > STRINGSIZE - 1) spaces = STRINGSIZE - 1;
            memset(*sval, ' ', spaces);
            (*sval)[spaces] = '\0';
            break;
        }
        case F_SPC: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            int n = arg1Ival;
            if (n < 0) n = 0;
            if (n > STRINGSIZE - 1) n = STRINGSIZE - 1;
            memset(*sval, ' ', n);
            (*sval)[n] = '\0';
            break;
        }
        case F_FORMAT: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            const char* fmt = hasArg2 ? arg2Sval : "%g";
            // Use integer or float based on format specifier
            if (strchr(fmt, 'd') || strchr(fmt, 'x') || strchr(fmt, 'X') ||
                strchr(fmt, 'u') || strchr(fmt, 'c') || strchr(fmt, 'o'))
                sprintf(*sval, fmt, arg1Ival);
            else
                sprintf(*sval, fmt, numVal);
            break;
        }
        case F_SCHANGE: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            if (arg1Type == T_STR && arg2Type == T_STR && arg3Type == T_STR) {
                char *src = arg1Sval;
                char *find = arg2Sval;
                char *repl = arg3Sval;
                int srcLen = strlen(src);
                int findLen = strlen(find);
                int replLen = strlen(repl);
                int di = 0;
                int si = 0;
                while (si < srcLen && di < STRINGSIZE - 1) {
                    if (findLen > 0 && strncmp(src + si, find, findLen) == 0 && 
                        (srcLen - si) >= findLen) {
                        for (int k = 0; k < replLen && di < STRINGSIZE - 1; k++)
                            (*sval)[di++] = repl[k];
                        si += findLen;
                    } else {
                        (*sval)[di++] = src[si++];
                    }
                }
                (*sval)[di] = '\0';
            } else {
                (*sval)[0] = '\0';
            }
            break;
        }
        case F_GPIO_PIN:
            *itype = T_INT;
            *ival = digitalRead(arg1Ival);
            *fval = *ival;
            break;
        case F_ADC:
            *itype = T_INT;
            *ival = analogRead(arg1Ival);
            *fval = *ival;
            break;
        case F_PULSIN: {
            int state = hasArg2 ? arg2Ival : HIGH;
            *itype = T_INT;
            *ival = pulseIn(arg1Ival, state ? HIGH : LOW);
            *fval = *ival;
            break;
        }
        case F_TOUCH:
            *itype = T_INT;
            *ival = touchRead(arg1Ival);
            *fval = *ival;
            break;
        case F_RGB:
            *itype = T_INT;
            *ival = ((arg1Ival & 0xF8) << 8) | ((arg2Ival & 0xFC) << 3) | ((arg3Ival & 0xF8) >> 3);
            *fval = *ival;
            break;
        case F_POINT:
            *itype = T_INT;
            *ival = M5Cardputer.Display.readPixel(arg1Ival, arg2Ival) & 0xFFFF;
            *fval = *ival;
            break;
        case F_EVAL: {
            *itype = T_INT;
            *ival = 0;
            if (arg1Type == T_STR && arg1Sval != NULL) {
                // Evaluate the expression string
                int eType, eIval;
                float eFval;
                char *eSval;
                char evalBuf[STRINGSIZE];
                strncpy(evalBuf, arg1Sval, STRINGSIZE - 1);
                evalBuf[STRINGSIZE - 1] = '\0';
                char *p = evalBuf;
                if (!MMBasic_EvaluateExpression(&p, &eType, &eIval, &eFval, &eSval)) {
                    *itype = eType;
                    *ival = eIval;
                    *fval = eFval;
                    if (eType == T_STR && eSval != NULL) {
                        *sval = MMBasic_GetTempString();
                        strcpy(*sval, eSval);
                    }
                }
            }
            *fval = (float)*ival;
            break;
        }
        case F_INKEY: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            (*sval)[0] = '\0';
            if (HAL_Keyboard_Available()) {
                char c = HAL_Keyboard_Read();
                if (c >= 32 || c == 13 || c == 10) {
                    (*sval)[0] = c;
                    (*sval)[1] = '\0';
                }
            }
            break;
        }
        case F_KEYDOWN: {
            *itype = T_INT;
            *ival = 0;
            // KEYDOWN returns 1 if a key is available, 0 otherwise
            if (arg1Ival == 0) {
                // No argument - check if any key available
                *ival = HAL_Keyboard_Available() ? 1 : 0;
            } else {
                // Check specific key code
                *ival = HAL_Keyboard_Available() ? 1 : 0;
            }
            *fval = (float)*ival;
            break;
        }
        case F_VERSION: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            strcpy(*sval, "6.03.00");
            break;
        }
        case F_TIMER: {
            *itype = T_INT;
            *ival = (int)millis();
            *fval = (float)*ival;
            break;
        }
        case F_MM_HRES: {
            *itype = T_INT;
            *ival = 240;
            *fval = 240.0f;
            break;
        }
        case F_MM_VRES: {
            *itype = T_INT;
            *ival = 135;
            *fval = 135.0f;
            break;
        }
        case F_MM_WIDTH: {
            *itype = T_INT;
            *ival = 40;  // 240 / 6 = 40 chars
            *fval = 40.0f;
            break;
        }
        case F_MM_HEIGHT: {
            *itype = T_INT;
            *ival = 16;  // 135 / 8 = 16.875, round down
            *fval = 16.0f;
            break;
        }
        case F_MM_HPOS: {
            *itype = T_INT;
            *ival = MMCharPos;
            *fval = (float)*ival;
            break;
        }
        case F_MM_VPOS: {
            *itype = T_INT;
            *ival = 0;  // TODO: track vertical position
            *fval = 0.0f;
            break;
        }
        case F_MM_DEVICE: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            strcpy(*sval, "M5Cardputer");
            break;
        }
        case F_MM_FONTWIDTH: {
            *itype = T_INT;
            *ival = 6;
            *fval = 6.0f;
            break;
        }
        case F_MM_FONTHEIGHT: {
            *itype = T_INT;
            *ival = 8;
            *fval = 8.0f;
            break;
        }
        case F_MM_ERRNO: {
            *itype = T_INT;
            *ival = 0;  // TODO: track error number
            *fval = 0.0f;
            break;
        }
        case F_MM_ERRMSG: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            (*sval)[0] = '\0';  // TODO: track error message
            break;
        }
        case F_MM_FLAGS: {
            *itype = T_INT;
            *ival = 0;  // TODO: user flags
            *fval = 0.0f;
            break;
        }
        case F_MM_DISPLAY: {
            *itype = T_INT;
            *ival = 1;  // LCD type
            *fval = 1.0f;
            break;
        }
        case F_MM_SUPPLY: {
            *itype = T_FLOAT;
            // Read battery voltage if available
            *fval = 3.3f;  // Default
            *ival = (int)*fval;
            break;
        }
        case F_MM_INFO: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            strcpy(*sval, "M5Cardputer ESP32-S3");
            break;
        }
        case F_CWD: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            strcpy(*sval, "/");
            break;
        }
        case F_DIR: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            (*sval)[0] = '\0';
            // TODO: Implement directory iteration
            break;
        }
        case F_FIELD: {
            *itype = T_STR;
            *sval = MMBasic_GetTempString();
            (*sval)[0] = '\0';
            // FIELD$(str$, n [, delim$])
            if (arg1Type == T_STR && arg1Sval != NULL) {
                int n = arg2Ival;
                char delim = (arg3Type == T_STR && arg3Sval != NULL && arg3Sval[0] != '\0') ? arg3Sval[0] : ',';
                char *p = arg1Sval;
                int field = 1;
                while (*p && field < n) {
                    if (*p == delim) field++;
                    p++;
                }
                if (field == n) {
                    int i = 0;
                    while (*p && *p != delim && i < STRINGSIZE - 1) {
                        (*sval)[i++] = *p++;
                    }
                    (*sval)[i] = '\0';
                }
            }
            break;
        }
        default:
            MMBasic_Error(ERR_SYNTAX, "Unknown function");
            return 1;
    }
    
    return 0;
}

// Public expression evaluator wrapper
int MMBasic_EvaluateExpression(char **expr, int *itype, int *ival, float *fval, char **sval) {
    return EvaluateExpression(expr, itype, ival, fval, sval);
}

// ============================================================================
// Command implementations are in MMBasic_cmds.cpp
// ============================================================================
