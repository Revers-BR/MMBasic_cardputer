#include "MMBasic.h"
#include "HAL.h"
#include <Wire.h>
#include <math.h>
#include <ctype.h>
void ScanSubFunDefs(void);
// External globals from MMBasic.cpp
// Struct definitions
struct s_forstack {
    int lineIdx;
    char varname[33];
    int varindex, toval, stepval;
};
struct s_filehandle { File file; bool inUse; bool isCom; int comPort; };
struct s_shadow { int varIndex; char type; int ival; float fval; char *sval; int ndims; int dims[MAXDIMS]; int *arr; bool wasConst; bool isStatic; };
struct s_subfun_entry { char name[33]; int startLine, endLine, paramCount; char paramNames[8][33]; bool isFunction; int returnLine; };
struct s_static_var { char name[MAXVARLEN+1]; int subfunIndex; char type; int ival; float fval; char sval[STRINGSIZE]; int ndims; int dims[MAXDIMS]; int *arr; };
struct s_select { int matchValue; char matchStr[STRINGSIZE]; char matchType; bool matched; };

extern char *progmem;
extern int progsize;
extern char *progptr;
extern char inpbuf[];
extern char tknbuf[];
extern struct s_vartbl *vartbl;
extern int varcnt;
extern struct s_subfun subfun[];
extern int subfunct;
extern jmp_buf mark;
extern int MMCharPos;
extern volatile int MMAbort;
extern char BreakKey;
extern char *currentLine;
extern int currentLineIndex;
extern bool flowControlActive;
extern bool traceOn;
extern int linecnt;
extern char *lines[1000];
extern int lineNumbers[1000];
extern int onErrorSkipCount;
extern int onErrorSkipActive;
#define MAX_LINES 1000
#define MAX_SHADOW 128
extern int forstackptr;
extern s_forstack forstack[16];
extern int gosubstack[16];
extern int gosubstackptr;
extern int whilestack[16];
extern int whilestackptr;
extern int dostack[16];
extern int dostackptr;
extern s_filehandle fileTable[5];
extern int currentDrive;
extern int dataLineIdx;
extern int dataOffset;
extern bool *varIsConst;
extern s_subfun_entry subFunTable[128];
extern int subFunCount;
extern int subFunCallReturnIdx;
extern s_shadow shadowStack[128];
extern int shadowPtr;
extern int shadowBase[16];
extern int shadowBaseSP;
extern int subFunRetStack[16];
extern s_select selectstack[8];
extern int selectptr;
extern char strpool[16][256];
extern int strpoolindex;

// Static variable storage and SUB tracking for LOCAL/STATIC support
#define MAX_STATIC_VARS 64
extern s_static_var staticVars[MAX_STATIC_VARS];
extern int staticVarCount;
extern int currentSubFunIdx;
extern int subFunIdxStack[16];
extern int subFunIdxSP;

// Command Implementations
// ============================================================================

// PRINT command
void MMBasic_CmdPrint(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Check for PRINT# (output to file)
    int fnbr = 0;
    if (*line == '#') {
        line++;
        while (*line == ' ') line++;
        int numType, numIval;
        float numFval;
        char *numSval;
        if (MMBasic_EvaluateExpression(&line, &numType, &numIval, &numFval, &numSval)) return;
        fnbr = numIval;
        if (fnbr < 1 || fnbr > MAXOPENFILES || !fileTable[fnbr].inUse) {
            MMBasic_Error(ERR_FILE_IO, "File not open");
            return;
        }
        // Skip comma after file number
        while (*line == ' ') line++;
        if (*line == ',') line++;
    }
    
    // Handle empty PRINT (just newline)
    while (*line == ' ') line++;
    if (*line == '\0' || *line == '\n' || *line == '\r') {
        if (fnbr) {
            if (fileTable[fnbr].isCom) {
                HardwareSerial *s = (fileTable[fnbr].comPort==1)?&Serial1:&Serial2;
                s->println("");
            } else { fileTable[fnbr].file.println(""); }
        } else {
            HAL_Display_Newline();
        }
        return;
    }
    
    while (*line != '\0' && *line != '\n' && *line != '\r') {
        // Skip whitespace
        while (*line == ' ') line++;
        
        if (*line == '\0' || *line == '\n' || *line == '\r') break;
        
        // Check for semicolon or comma
        if (*line == ';') {
            line++;
            continue;
        }
        if (*line == ',') {
            if (fnbr) {
                fileTable[fnbr].file.print('\t');
            } else {
                int pos = MMCharPos;
                int nextTab = ((pos / 10) + 1) * 10;
                while (pos < nextTab) {
                    HAL_Display_Print(" ");
                    pos++;
                }
            }
            line++;
            continue;
        }
        
        // Evaluate expression
        int itype, ival;
        float fval;
        char *sval;
        
        if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) {
            return; // Error
        }
        
        // Print the value
        if (fnbr) {
            HardwareSerial *ser = fileTable[fnbr].isCom ? 
                ((fileTable[fnbr].comPort==1)?&Serial1:&Serial2) : NULL;
            switch (itype) {
                case T_INT:
                    if (ser) ser->print(ival); else fileTable[fnbr].file.print(ival);
                    break;
                case T_FLOAT:
                    if (ser) ser->print(fval); else fileTable[fnbr].file.print(fval);
                    break;
                case T_STR:
                    if (ser) ser->print(sval); else if (sval) fileTable[fnbr].file.print(sval);
                    break;
            }
        } else {
            switch (itype) {
                case T_INT: {
                    char buf[16];
                    sprintf(buf, "%d", ival);
                    HAL_Display_Print(buf);
                    break;
                }
                case T_FLOAT: {
                    char buf[32];
                    sprintf(buf, "%g", fval);
                    HAL_Display_Print(buf);
                    break;
                }
                case T_STR:
                    if (sval != NULL) {
                        HAL_Display_Print(sval);
                    }
                    break;
            }
        }
    }
    
    // Check if line ends with semicolon (suppress newline)
    char *end = line;
    while (*end == ' ') end++;
    if (*end == ';') {
        // Don't print newline
    } else {
        if (fnbr) {
            if (fileTable[fnbr].isCom) {
                HardwareSerial *s = (fileTable[fnbr].comPort==1)?&Serial1:&Serial2;
                s->println("");
            } else { fileTable[fnbr].file.println(""); }
        } else {
            HAL_Display_Newline();
        }
    }
}

// INPUT command
void MMBasic_CmdInput(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Check for INPUT# (read from file)
    int fnbr = 0;
    char prompt[STRINGSIZE] = "? ";
    
    if (*line == '#') {
        line++;
        while (*line == ' ') line++;
        int numType, numIval;
        float numFval;
        char *numSval;
        if (MMBasic_EvaluateExpression(&line, &numType, &numIval, &numFval, &numSval)) return;
        fnbr = numIval;
        if (fnbr < 1 || fnbr > MAXOPENFILES || !fileTable[fnbr].inUse) {
            MMBasic_Error(ERR_FILE_IO, "File not open");
            return;
        }
        // Skip comma
        while (*line == ' ') line++;
        if (*line == ',') line++;
    } else {
        // Check for prompt string (console input only)
        if (*line == '"') {
            line++;
            int i = 0;
            while (*line != '"' && *line != '\0' && i < STRINGSIZE - 1) {
                prompt[i++] = *line++;
            }
            prompt[i] = '\0';
            if (*line == '"') line++;
            
            // Check for semicolon after prompt
            while (*line == ' ') line++;
            if (*line == ';') {
                line++;
            }
        }
    }
    
    // Get variable name(s)
    while (*line == ' ') line++;
    
    if (fnbr) {
        // INPUT# from file - read a line and parse into variables
        while (*line != '\0' && *line != '\n' && *line != '\r') {
            while (*line == ' ') line++;
            if (*line == '\0' || *line == '\n' || *line == '\r') break;
            
            char varName[MAXVARLEN + 2];
            int i = 0;
            while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
                   (*line >= '0' && *line <= '9') || *line == '_' || *line == '$') {
                if (i < MAXVARLEN + 1) varName[i++] = *line;
                line++;
            }
            varName[i] = '\0';
            
            for (int j = 0; varName[j]; j++) {
                if (varName[j] >= 'a' && varName[j] <= 'z') varName[j] -= 32;
            }
            
            // Skip comma between variables
            while (*line == ' ') line++;
            if (*line == ',') line++;
            
            if (varName[0] == '\0') continue;
            
            // Read next value from file
            char fileBuf[STRINGSIZE];
            int bi = 0;
            char ch;
            // Skip leading whitespace
            while (fileTable[fnbr].file.available()) {
                ch = fileTable[fnbr].file.read();
                if (ch != ' ' && ch != '\t') {
                    fileBuf[bi++] = ch;
                    break;
                }
            }
            if (bi == 0 && !fileTable[fnbr].file.available()) break;
            
            // Read until comma, newline, or end
            while (fileTable[fnbr].file.available() && bi < STRINGSIZE - 1) {
                ch = fileTable[fnbr].file.peek();
                if (ch == ',' || ch == '\n' || ch == '\r') break;
                fileBuf[bi++] = fileTable[fnbr].file.read();
            }
            // Skip the comma separator if present
            if (fileTable[fnbr].file.available()) {
                ch = fileTable[fnbr].file.peek();
                if (ch == ',') fileTable[fnbr].file.read();
            }
            // Trim trailing whitespace
            while (bi > 0 && (fileBuf[bi-1] == ' ' || fileBuf[bi-1] == '\r')) bi--;
            fileBuf[bi] = '\0';
            
            // Find or create variable
            int varIdx = MMBasic_FindVariable(varName);
            if (varIdx < 0) {
                char type = T_INT;
                if (varName[strlen(varName) - 1] == '$') type = T_STR;
                varIdx = MMBasic_CreateVariable(varName, type);
                if (varIdx < 0) return;
            }
            
            switch (vartbl[varIdx].type) {
                case T_INT:
                    vartbl[varIdx].val.ival = atoi(fileBuf);
                    break;
                case T_FLOAT:
                    vartbl[varIdx].val.fval = atof(fileBuf);
                    break;
                case T_STR:
                    strncpy(vartbl[varIdx].val.sval, fileBuf, STRINGSIZE - 1);
                    break;
            }
        }
    } else {
        // Console INPUT - supports multiple variables: INPUT A, B, C$
        
        // Collect all variable names first
        char varNames[16][MAXVARLEN + 2]; // Max 16 variables
        int varCount = 0;
        
        while (*line != '\0' && *line != '\n' && *line != '\r') {
            while (*line == ' ') line++;
            if (*line == '\0' || *line == '\n' || *line == '\r') break;
            
            char *vn = varNames[varCount];
            int i = 0;
            while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
                   (*line >= '0' && *line <= '9') || *line == '_' || *line == '$') {
                if (i < MAXVARLEN + 1) vn[i++] = *line;
                line++;
            }
            vn[i] = '\0';
            
            for (int j = 0; vn[j]; j++) {
                if (vn[j] >= 'a' && vn[j] <= 'z') vn[j] -= 32;
            }
            
            if (vn[0] != '\0') varCount++;
            if (varCount >= 16) break;
            
            while (*line == ' ') line++;
            if (*line == ',') line++;
        }
        
        if (varCount == 0) {
            MMBasic_Error(ERR_SYNTAX, "Expected variable");
            return;
        }
        
        // Display prompt once
        HAL_Display_Print(prompt);
        
        // Get one line of input
        char input[STRINGSIZE];
        HAL_Keyboard_GetLine(input, STRINGSIZE);
        
        // Parse input into values, split by commas
        char *inp = input;
        for (int v = 0; v < varCount; v++) {
            char value[STRINGSIZE];
            int vi = 0;
            
            // Skip leading spaces
            while (*inp == ' ') inp++;
            
            // If quoted string
            if (*inp == '"') {
                inp++;
                while (*inp != '"' && *inp != '\0' && vi < STRINGSIZE - 1) {
                    value[vi++] = *inp++;
                }
                if (*inp == '"') inp++;
            } else {
                // Read until comma or end
                while (*inp != '\0' && *inp != ',' && vi < STRINGSIZE - 1) {
                    value[vi++] = *inp++;
                }
                // Trim trailing spaces
                while (vi > 0 && value[vi-1] == ' ') vi--;
            }
            value[vi] = '\0';
            
            // Skip comma separator
            while (*inp == ' ') inp++;
            if (*inp == ',') inp++;
            
            // Find or create variable
            int varIdx = MMBasic_FindVariable(varNames[v]);
            if (varIdx < 0) {
                char type = T_INT;
                int nlen = strlen(varNames[v]);
                if (nlen > 0 && varNames[v][nlen - 1] == '$') type = T_STR;
                varIdx = MMBasic_CreateVariable(varNames[v], type);
                if (varIdx < 0) return;
            }
            
            // Assign value
            switch (vartbl[varIdx].type) {
                case T_INT:
                    vartbl[varIdx].val.ival = atoi(value);
                    break;
                case T_FLOAT:
                    vartbl[varIdx].val.fval = atof(value);
                    break;
                case T_STR:
                    strncpy(vartbl[varIdx].val.sval, value, STRINGSIZE - 1);
                    break;
            }
        }
    }
}

// LET command (explicit)
void MMBasic_CmdLet(void) {
    char *line = currentLine;

    // Skip whitespace
    while (*line == ' ') line++;

    // Get variable name (including type suffix !, %, $)
    char varName[MAXVARLEN + 2];
    int i = 0;
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') ||
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
           *line == '!' || *line == '%') {
        if (i < MAXVARLEN + 1) varName[i++] = *line;
        line++;
    }
    varName[i] = '\0';

    // Convert to uppercase (but preserve suffix characters)
    for (int j = 0; varName[j]; j++) {
        if (varName[j] >= 'a' && varName[j] <= 'z') {
            varName[j] = varName[j] - 'a' + 'A';
        }
    }

    if (varName[0] == '\0') {
        MMBasic_Error(ERR_SYNTAX, "Expected variable");
        return;
    }

    // Find or create variable (need it early for array check)
    int varIdx = MMBasic_FindVariable(varName);
    if (varIdx < 0) {
        char type = MMBasic_GetVarType(varName);
        varIdx = MMBasic_CreateVariable(varName, type);
        if (varIdx < 0) return;
    }

    // Check for array element assignment: A(i) = expr or A(i,j) = expr
    int arrFlatIdx = -1;
    while (*line == ' ') line++;
    if (vartbl[varIdx].ndims > 0 && *line == '(') {
        line++; // skip '('
        int indices[MAXDIMS] = {0};
        int nIdx = 0;
        while (nIdx < MAXDIMS) {
            int idxType, idxIval;
            float idxFval;
            char *idxSval;
            if (MMBasic_EvaluateExpression(&line, &idxType, &idxIval, &idxFval, &idxSval)) return;
            indices[nIdx++] = idxIval;
            while (*line == ' ') line++;
            if (*line == ',') { line++; while (*line == ' ') line++; }
            else break;
        }
        while (*line == ' ') line++;
        if (*line == ')') line++;
        else { MMBasic_Error(ERR_SYNTAX, "Expected )"); return; }

        // Calculate flat index
        arrFlatIdx = 0;
        for (int d = 0; d < nIdx; d++) {
            int stride = 1;
            for (int s = d + 1; s < vartbl[varIdx].ndims; s++) {
                stride *= (vartbl[varIdx].dims[s] + 1);
            }
            arrFlatIdx += indices[d] * stride;
        }

        // Bounds check
        int totalSize = 1;
        for (int d = 0; d < vartbl[varIdx].ndims; d++) totalSize *= (vartbl[varIdx].dims[d] + 1);
        if (arrFlatIdx < 0 || arrFlatIdx >= totalSize) {
            MMBasic_Error(ERR_BOUNDS, "Array index out of bounds");
            return;
        }
    }

    // Skip whitespace and expect =
    while (*line == ' ') line++;
    if (*line != '=') {
        MMBasic_Error(ERR_SYNTAX, "Expected =");
        return;
    }
    line++;

    // Evaluate expression
    int itype, ival;
    float fval;
    char *sval;

    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) {
        return; // Error
    }

    // Check const
    if (varIsConst[varIdx]) {
        MMBasic_Error(ERR_SYNTAX, "Cannot change constant");
        return;
    }

    // Set value
    if (arrFlatIdx >= 0) {
        // Array element assignment - handle different types
        if (vartbl[varIdx].type == T_FLOAT) {
            ((float *)vartbl[varIdx].arr)[arrFlatIdx] = (itype == T_FLOAT) ? fval : (float)ival;
        } else if (vartbl[varIdx].type == T_STR) {
            // String array - store pointer
            if (sval != NULL) {
                ((char **)vartbl[varIdx].arr)[arrFlatIdx] = sval;
            }
        } else {
            vartbl[varIdx].arr[arrFlatIdx] = ival;
        }
    } else {
        MMBasic_SetVariable(varIdx, ival, fval, sval);
    }
}

// Implicit LET (variable = expression without LET keyword)
void MMBasic_CmdImplicitLet(char *line) {
    // This is called when the command is not recognized
    // Try to parse as variable = expression
    
    // Save position
    char *start = line;
    
    // Skip whitespace
    while (*line == ' ') line++;

    // Check for MID$() = assignment: MID$(varname, pos [, len]) = expr
    if ((line[0] == 'M' || line[0] == 'm') && (line[1] == 'I' || line[1] == 'i') &&
        (line[2] == 'D' || line[2] == 'd') && line[3] == '$' && line[4] == '(') {
        line += 5; // skip "MID$("
        while (*line == ' ') line++;

        // Parse variable name
        char midVarName[MAXVARLEN + 2];
        int mi = 0;
        while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') ||
               (*line >= '0' && *line <= '9') || *line == '_' || *line == '$') {
            if (mi < MAXVARLEN + 1) midVarName[mi++] = *line;
            line++;
        }
        midVarName[mi] = '\0';
        for (int j = 0; midVarName[j]; j++)
            if (midVarName[j] >= 'a' && midVarName[j] <= 'z') midVarName[j] -= 32;

        while (*line == ' ') line++;
        if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
        line++;

        // Parse start position (1-based)
        int midType, midPos;
        float midFval;
        char *midSval;
        if (MMBasic_EvaluateExpression(&line, &midType, &midPos, &midFval, &midSval)) return;

        // Parse optional length
        int midLen = -1; // -1 means use replacement length
        while (*line == ' ') line++;
        if (*line == ',') {
            line++;
            int lt;
            if (MMBasic_EvaluateExpression(&line, &lt, &midLen, &midFval, &midSval)) return;
        }

        // Expect closing )
        while (*line == ' ') line++;
        if (*line != ')') { MMBasic_Error(ERR_SYNTAX, "Expected )"); return; }
        line++;

        // Expect =
        while (*line == ' ') line++;
        if (*line != '=') { MMBasic_Error(ERR_SYNTAX, "Expected ="); return; }
        line++;

        // Evaluate replacement expression
        int rType, rIval;
        float rFval;
        char *rSval;
        if (MMBasic_EvaluateExpression(&line, &rType, &rIval, &rFval, &rSval)) return;
        if (rType != T_STR || rSval == NULL) { MMBasic_Error(ERR_TYPE, "Expected string"); return; }

        // Find the variable
        int midIdx = MMBasic_FindVariable(midVarName);
        if (midIdx < 0 || vartbl[midIdx].type != T_STR) {
            MMBasic_Error(ERR_TYPE, "Expected string variable");
            return;
        }
        if (varIsConst[midIdx]) { MMBasic_Error(ERR_SYNTAX, "Cannot change constant"); return; }

        // Perform mid-replacement
        char *dst = vartbl[midIdx].val.sval;
        int dstLen = strlen(dst);
        int replLen = strlen(rSval);
        int pos0 = midPos - 1; // 0-based
        if (pos0 < 0) pos0 = 0;
        int len = (midLen < 0) ? replLen : midLen;
        if (len > replLen) len = replLen;
        if (pos0 > dstLen) pos0 = dstLen;
        if (pos0 + len > STRINGSIZE - 1) len = STRINGSIZE - 1 - pos0;

        // Overwrite characters in place
        for (int k = 0; k < len && rSval[k] != '\0'; k++) {
            dst[pos0 + k] = rSval[k];
        }
        // dst is not shortened or extended by MID$= in standard BASIC
        return;
    }

    // Get variable name (including type suffix !, %, $)
    char varName[MAXVARLEN + 2];
    int i = 0;
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') ||
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
           *line == '!' || *line == '%') {
        if (i < MAXVARLEN + 1) varName[i++] = *line;
        line++;
    }
    varName[i] = '\0';

    // Convert to uppercase (but preserve suffix characters)
    for (int j = 0; varName[j]; j++) {
        if (varName[j] >= 'a' && varName[j] <= 'z') {
            varName[j] = varName[j] - 'a' + 'A';
        }
    }

    // Find or create variable (need it early for array check)
    int varIdx = MMBasic_FindVariable(varName);
    if (varIdx < 0) {
        char type = MMBasic_GetVarType(varName);
        varIdx = MMBasic_CreateVariable(varName, type);
        if (varIdx < 0) return;
    }

    // Check for array element assignment: A(i) = expr or A(i,j) = expr
    int arrFlatIdx = -1;
    while (*line == ' ') line++;
    if (vartbl[varIdx].ndims > 0 && *line == '(') {
        line++; // skip '('
        int indices[MAXDIMS] = {0};
        int nIdx = 0;
        while (nIdx < MAXDIMS) {
            int idxType, idxIval;
            float idxFval;
            char *idxSval;
            if (MMBasic_EvaluateExpression(&line, &idxType, &idxIval, &idxFval, &idxSval)) return;
            indices[nIdx++] = idxIval;
            while (*line == ' ') line++;
            if (*line == ',') { line++; while (*line == ' ') line++; }
            else break;
        }
        while (*line == ' ') line++;
        if (*line == ')') line++;
        else { MMBasic_Error(ERR_SYNTAX, "Expected )"); return; }

        // Calculate flat index
        arrFlatIdx = 0;
        for (int d = 0; d < nIdx; d++) {
            int stride = 1;
            for (int s = d + 1; s < vartbl[varIdx].ndims; s++) {
                stride *= (vartbl[varIdx].dims[s] + 1);
            }
            arrFlatIdx += indices[d] * stride;
        }

        // Bounds check
        int totalSize = 1;
        for (int d = 0; d < vartbl[varIdx].ndims; d++) totalSize *= (vartbl[varIdx].dims[d] + 1);
        if (arrFlatIdx < 0 || arrFlatIdx >= totalSize) {
            MMBasic_Error(ERR_BOUNDS, "Array index out of bounds");
            return;
        }
    }

    // Check for = sign
    while (*line == ' ') line++;
    if (*line != '=') {
        MMBasic_Error(ERR_UNKNOWN_CMD, NULL);
        return;
    }
    line++;

    // Evaluate expression
    int itype, ival;
    float fval;
    char *sval;

    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) {
        return; // Error
    }

    // Check const
    if (varIsConst[varIdx]) {
        MMBasic_Error(ERR_SYNTAX, "Cannot change constant");
        return;
    }

    // Set value
    if (arrFlatIdx >= 0) {
        // Array element assignment - handle different types
        if (vartbl[varIdx].type == T_FLOAT) {
            ((float *)vartbl[varIdx].arr)[arrFlatIdx] = (itype == T_FLOAT) ? fval : (float)ival;
        } else if (vartbl[varIdx].type == T_STR) {
            // String array - store pointer
            if (sval != NULL) {
                ((char **)vartbl[varIdx].arr)[arrFlatIdx] = sval;
            }
        } else {
            vartbl[varIdx].arr[arrFlatIdx] = ival;
        }
    } else {
        MMBasic_SetVariable(varIdx, ival, fval, sval);
    }
}

// IF command
void MMBasic_CmdIf(void) {
    char *line = currentLine;
    
    // Evaluate condition
    int itype, ival;
    float fval;
    char *sval;
    
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) {
        return; // Error
    }
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Check for THEN (case-insensitive)
    if ((line[0] == 'T' || line[0] == 't') && (line[1] == 'H' || line[1] == 'h') &&
        (line[2] == 'E' || line[2] == 'e') && (line[3] == 'N' || line[3] == 'n')) {
        line += 4;
        while (*line == ' ') line++;
        
        if (ival) {
            // Execute THEN part
            MMBasic_Execute(line);
        }
    } else {
        // Simple IF without THEN
        if (ival) {
            MMBasic_Execute(line);
        }
    }
}

// FOR command
void MMBasic_CmdFor(void) {
    char *line = currentLine;
    
    // Get variable name
    while (*line == ' ') line++;
    
    char varName[MAXVARLEN + 2];
    int i = 0;
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$') {
        if (i < MAXVARLEN + 1) varName[i++] = *line;
        line++;
    }
    varName[i] = '\0';
    
    // Convert to uppercase
    for (int j = 0; varName[j]; j++) {
        if (varName[j] >= 'a' && varName[j] <= 'z') {
            varName[j] = varName[j] - 'a' + 'A';
        }
    }
    
    // Expect =
    while (*line == ' ') line++;
    if (*line != '=') {
        MMBasic_Error(ERR_SYNTAX, "Expected =");
        return;
    }
    line++;
    
    // Get start value
    int startType, startIval;
    float startFval;
    char *startSval;
    if (MMBasic_EvaluateExpression(&line, &startType, &startIval, &startFval, &startSval)) return;
    
    // Expect TO or to (case-insensitive)
    while (*line == ' ') line++;
    if ((line[0] != 'T' && line[0] != 't') || (line[1] != 'O' && line[1] != 'o')) {
        MMBasic_Error(ERR_SYNTAX, "Expected TO");
        return;
    }
    line += 2;
    
    // Get end value
    int endType, endIval;
    float endFval;
    char *endSval;
    if (MMBasic_EvaluateExpression(&line, &endType, &endIval, &endFval, &endSval)) return;
    
    // Get optional STEP
    int stepIval = 1;
    float stepFval = 1.0;
    while (*line == ' ') line++;
    if ((line[0] == 'S' || line[0] == 's') && (line[1] == 'T' || line[1] == 't') &&
        (line[2] == 'E' || line[2] == 'e') && (line[3] == 'P' || line[3] == 'p')) {
        line += 4;
        int stepType;
        char *stepSval;
        if (MMBasic_EvaluateExpression(&line, &stepType, &stepIval, &stepFval, &stepSval)) return;
    }
    
    // Find or create variable
    int varIdx = MMBasic_FindVariable(varName);
    if (varIdx < 0) {
        varIdx = MMBasic_CreateVariable(varName, T_INT);
        if (varIdx < 0) return;
    }
    
    // Set initial value
    MMBasic_SetVariable(varIdx, startIval, startFval, startSval);
    
    // Push to FOR stack
    if (forstackptr >= MAXFORLOOPS) {
        MMBasic_Error(ERR_STACK_FULL, "FOR stack full");
        return;
    }
    
    forstack[forstackptr].lineIdx = currentLineIndex + 1; // Loop body starts at next line
    strcpy(forstack[forstackptr].varname, varName);
    forstack[forstackptr].varindex = varIdx;
    forstack[forstackptr].toval = endIval;
    forstack[forstackptr].stepval = stepIval;
    forstackptr++;
}

// NEXT command
void MMBasic_CmdNext(void) {
    char *line = currentLine;
    
    // Get optional variable name
    while (*line == ' ') line++;
    
    char varName[MAXVARLEN + 2] = "";
    if (*line != '\0' && *line != '\n' && *line != '\r') {
        int i = 0;
        while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
               (*line >= '0' && *line <= '9') || *line == '_' || *line == '$') {
            if (i < MAXVARLEN + 1) varName[i++] = *line;
            line++;
        }
        varName[i] = '\0';
        
        // Convert to uppercase
        for (int j = 0; varName[j]; j++) {
            if (varName[j] >= 'a' && varName[j] <= 'z') {
                varName[j] = varName[j] - 'a' + 'A';
            }
        }
    }
    
    // Find matching FOR
    if (forstackptr == 0) {
        MMBasic_Error(ERR_FOR_NEXT, "NEXT without FOR");
        return;
    }
    
    // If variable name specified, find matching FOR
    int stackIdx = forstackptr - 1;
    if (varName[0] != '\0') {
        // Search backwards for matching variable
        for (stackIdx = forstackptr - 1; stackIdx >= 0; stackIdx--) {
            if (strcmp(forstack[stackIdx].varname, varName) == 0) {
                break;
            }
        }
        if (stackIdx < 0) {
            MMBasic_Error(ERR_FOR_NEXT, "NEXT without matching FOR");
            return;
        }
    }
    
    // Increment variable
    int varIdx = forstack[stackIdx].varindex;
    int newVal = vartbl[varIdx].val.ival + forstack[stackIdx].stepval;
    vartbl[varIdx].val.ival = newVal;
    
    // Check if loop should continue
    int continueLoop = 0;
    if (forstack[stackIdx].stepval > 0) {
        continueLoop = (newVal <= forstack[stackIdx].toval);
    } else {
        continueLoop = (newVal >= forstack[stackIdx].toval);
    }
    
    if (continueLoop) {
        // Loop back to line after FOR
        currentLineIndex = forstack[stackIdx].lineIdx;
        currentLine = lines[currentLineIndex];
        flowControlActive = true;
    } else {
        // Loop finished, remove from stack
        forstackptr = stackIdx;
    }
}

// GOTO command
void MMBasic_CmdGoto(void) {
    char *line = currentLine;
    
    // Get line number
    while (*line == ' ') line++;
    
    if (*line < '0' || *line > '9') {
        MMBasic_Error(ERR_SYNTAX, "Expected line number");
        return;
    }
    
    int targetLine = atoi(line);
    
    // Find the line
    for (int i = 0; i < linecnt; i++) {
        if (lineNumbers[i] == targetLine) {
            currentLineIndex = i;
            currentLine = lines[i];
            flowControlActive = true;
            return;
        }
    }
    
    MMBasic_Error(ERR_SYNTAX, "Line not found");
}

// GOSUB command
void MMBasic_CmdGosub(void) {
    char *line = currentLine;
    
    // Get line number
    while (*line == ' ') line++;
    
    if (*line < '0' || *line > '9') {
        MMBasic_Error(ERR_SYNTAX, "Expected line number");
        return;
    }
    
    int targetLine = atoi(line);
    
    // Push return line index (line after GOSUB)
    if (gosubstackptr >= MAXGOSUB) {
        MMBasic_Error(ERR_STACK_FULL, "GOSUB stack full");
        return;
    }
    
    gosubstack[gosubstackptr++] = currentLineIndex + 1;
    
    // Find the target line
    for (int i = 0; i < linecnt; i++) {
        if (lineNumbers[i] == targetLine) {
            currentLineIndex = i;
            currentLine = lines[i];
            flowControlActive = true;
            return;
        }
    }
    
    MMBasic_Error(ERR_SYNTAX, "Line not found");
}

// ON GOTO/GOSUB command, and ON ERROR SKIP
extern int onErrorSkipCount;

void MMBasic_CmdOn(void) {
    char *line = currentLine;
    while (*line == ' ') line++;

    // Check for ON ERROR SKIP [n]
    if ((line[0]=='E'||line[0]=='e') && (line[1]=='R'||line[1]=='r') &&
        (line[2]=='R'||line[2]=='r') && (line[3]=='O'||line[3]=='o') &&
        (line[4]=='R'||line[4]=='r') && !isalpha((unsigned char)line[5])) {
        line += 5;
        while (*line == ' ') line++;
        // Check for SKIP keyword
        if ((line[0]=='S'||line[0]=='s') && (line[1]=='K'||line[1]=='k') &&
            (line[2]=='I'||line[2]=='i') && (line[3]=='P'||line[3]=='p') &&
            !isalpha((unsigned char)line[4])) {
            line += 4;
            while (*line == ' ') line++;
            int n = 1; // default: skip 1 line
            if (*line >= '0' && *line <= '9') {
                int t; float f; char *s;
                if (MMBasic_EvaluateExpression(&line, &t, &n, &f, &s)) return;
                if (n < 1) n = 1;
            }
            onErrorSkipCount = n;
            return;
        }
        // ON ERROR without SKIP - just clear any handler
        onErrorSkipCount = 0;
        return;
    }

    // Evaluate the expression
    int itype, ival;
    float fval;
    char *sval;

    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
    int index = (itype == T_INT) ? ival : (int)fval;

    // Skip whitespace
    while (*line == ' ') line++;

    // Determine GOTO or GOSUB
    int isGosub = 0;
    if (strncmp(line, "GOTO", 4) == 0 && !isalpha((unsigned char)line[4])) {
        line += 4;
    } else if (strncmp(line, "GOSUB", 5) == 0 && !isalpha((unsigned char)line[5])) {
        line += 5;
        isGosub = 1;
    } else {
        MMBasic_Error(ERR_SYNTAX, "Expected GOTO or GOSUB");
        return;
    }

    // Parse comma-separated line numbers
    int targets[32];
    int count = 0;

    while (*line && count < 32) {
        while (*line == ' ') line++;
        if (*line < '0' || *line > '9') break;
        targets[count++] = atoi(line);
        while (*line >= '0' && *line <= '9') line++;
        while (*line == ' ') line++;
        if (*line == ',') line++;
    }

    // If index is 0 or > count, fall through to next line
    if (index <= 0 || index > count) return;

    int targetLine = targets[index - 1];

    // For GOSUB, push return address
    if (isGosub) {
        if (gosubstackptr >= MAXGOSUB) {
            MMBasic_Error(ERR_STACK_FULL, "GOSUB stack full");
            return;
        }
        gosubstack[gosubstackptr++] = currentLineIndex + 1;
    }

    // Find the target line
    for (int i = 0; i < linecnt; i++) {
        if (lineNumbers[i] == targetLine) {
            currentLineIndex = i;
            currentLine = lines[i];
            flowControlActive = true;
            return;
        }
    }

    MMBasic_Error(ERR_SYNTAX, "Line not found");
}

// RETURN command
void MMBasic_CmdReturn(void) {
    // SUB/FUNCTION return: restore shadow stack
    if (shadowBaseSP > 0) {
        shadowBaseSP--;
        while (shadowPtr > shadowBase[shadowBaseSP]) {
            shadowPtr--;
            int vi = shadowStack[shadowPtr].varIndex;
            if (vi >= 0) {
                // If this is a STATIC variable, save its current value to static storage
                if (shadowStack[shadowPtr].isStatic && currentSubFunIdx >= 0) {
                    // Find or create static storage entry
                    int si = -1;
                    for (int k = 0; k < staticVarCount; k++) {
                        if (staticVars[k].subfunIndex == currentSubFunIdx &&
                            strcmp(staticVars[k].name, vartbl[vi].name) == 0) {
                            si = k; break;
                        }
                    }
                    if (si < 0 && staticVarCount < MAX_STATIC_VARS) {
                        si = staticVarCount++;
                        strncpy(staticVars[si].name, vartbl[vi].name, MAXVARLEN);
                        staticVars[si].name[MAXVARLEN] = '\0';
                        staticVars[si].subfunIndex = currentSubFunIdx;
                    }
                    if (si >= 0) {
                        staticVars[si].type = vartbl[vi].type;
                        staticVars[si].ival = vartbl[vi].val.ival;
                        staticVars[si].fval = vartbl[vi].val.fval;
                        if (vartbl[vi].type == T_STR && vartbl[vi].val.sval) {
                            strncpy(staticVars[si].sval, vartbl[vi].val.sval, STRINGSIZE - 1);
                            staticVars[si].sval[STRINGSIZE - 1] = '\0';
                        } else {
                            staticVars[si].sval[0] = '\0';
                        }
                        staticVars[si].ndims = vartbl[vi].ndims;
                        memcpy(staticVars[si].dims, vartbl[vi].dims, sizeof(vartbl[vi].dims));
                        staticVars[si].arr = vartbl[vi].arr;
                    }
                }
                // Restore variable from shadow stack
                if (vartbl[vi].type==T_STR && vartbl[vi].val.sval) {
                    free(vartbl[vi].val.sval); vartbl[vi].val.sval=NULL;
                }
                vartbl[vi].type = shadowStack[shadowPtr].type;
                vartbl[vi].val.ival = shadowStack[shadowPtr].ival;
                vartbl[vi].val.fval = shadowStack[shadowPtr].fval;
                vartbl[vi].ndims = shadowStack[shadowPtr].ndims;
                memcpy(vartbl[vi].dims, shadowStack[shadowPtr].dims, sizeof(vartbl[vi].dims));
                vartbl[vi].arr = shadowStack[shadowPtr].arr;
                varIsConst[vi] = shadowStack[shadowPtr].wasConst;
                if (shadowStack[shadowPtr].sval) {
                    vartbl[vi].val.sval = shadowStack[shadowPtr].sval;
                }
            }
        }
        // Restore previous SUB index
        if (subFunIdxSP > 0) currentSubFunIdx = subFunIdxStack[--subFunIdxSP];
        else currentSubFunIdx = -1;
        int ret = -1;
        if (shadowBaseSP < 16) {
            ret = subFunRetStack[shadowBaseSP];
            subFunRetStack[shadowBaseSP] = -1;
        }
        if (ret < 0) { currentLineIndex++; } // No return addr, advance
        else {
            currentLineIndex = ret + 1;
            currentLine = (currentLineIndex < linecnt) ? lines[currentLineIndex] : NULL;
        }
        flowControlActive = true;
        return;
    }
    // Standard GOSUB return
    if (gosubstackptr == 0) {
        MMBasic_Error(ERR_RETURN, "RETURN without GOSUB");
        return;
    }
    currentLineIndex = gosubstack[--gosubstackptr] + 1;
    currentLine = (currentLineIndex < linecnt) ? lines[currentLineIndex] : NULL;
    flowControlActive = true;
}

// END command
void MMBasic_CmdEnd(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    if ((line[0]=='S'||line[0]=='s')&&(line[1]=='U'||line[1]=='u')&&(line[2]=='B'||line[2]=='b')) {
        MMBasic_CmdEndSubFun(); return;
    }
    if ((line[0]=='F'||line[0]=='f')&&(line[1]=='U'||line[1]=='u')&&(line[2]=='N'||line[2]=='n')) {
        MMBasic_CmdEndSubFun(); return;
    }
    if ((line[0]=='S'||line[0]=='s')&&(line[1]=='E'||line[1]=='e')&&(line[2]=='L'||line[2]=='l')) {
        MMBasic_CmdEndSelect(); return;
    }
    // In a running program, exit cleanly
    if (currentLineIndex >= 0 && currentLineIndex < linecnt) {
        currentLineIndex = linecnt;
        flowControlActive = true;
        return;
    }
    // Direct mode: longjmp to command prompt
    longjmp(mark, 1);
}

// SAVE command
void MMBasic_CmdSave(void) {
    char *line = currentLine;
    
    // Get filename
    while (*line == ' ') line++;
    
    char filename[FILENAME_LENGTH];
    if (*line == '"') {
        line++;
        int i = 0;
        while (*line != '"' && *line != '\0' && i < FILENAME_LENGTH - 1) {
            filename[i++] = *line++;
        }
        filename[i] = '\0';
    } else {
        int i = 0;
        while (*line != ' ' && *line != '\0' && i < FILENAME_LENGTH - 1) {
            filename[i++] = *line++;
        }
        filename[i] = '\0';
    }
    
    MMBasic_SaveProgram(filename);
}

// LOAD command
void MMBasic_CmdLoad(void) {
    char *line = currentLine;
    
    // Get filename
    while (*line == ' ') line++;
    
    char filename[FILENAME_LENGTH];
    if (*line == '"') {
        line++;
        int i = 0;
        while (*line != '"' && *line != '\0' && i < FILENAME_LENGTH - 1) {
            filename[i++] = *line++;
        }
        filename[i] = '\0';
    } else {
        int i = 0;
        while (*line != ' ' && *line != '\0' && i < FILENAME_LENGTH - 1) {
            filename[i++] = *line++;
        }
        filename[i] = '\0';
    }
    
    MMBasic_LoadProgram(filename);
}

// FILES command
void MMBasic_CmdFiles(void) {
    if (!HAL_SD_Init()) {
        HAL_Display_Println("SD card not available");
        return;
    }
    
    HAL_Display_Println("Files on SD card:");
    
    File root = HAL_SD_Open("/", "r");
    if (!root) {
        HAL_Display_Println("Failed to open root directory");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        if (file.isDirectory()) {
            HAL_Display_Print("  [DIR] ");
            HAL_Display_Println(file.name());
        } else {
            HAL_Display_Print("  ");
            HAL_Display_Print(file.name());
            HAL_Display_Print("  ");
            char sizeStr[16];
            sprintf(sizeStr, "%d bytes", file.size());
            HAL_Display_Println(sizeStr);
        }
        file = root.openNextFile();
    }
    
    root.close();
}

// Store a program line (insert, replace, or delete)
void MMBasic_StoreLine(int linenum, char *line) {
    // Find insertion point
    int i;
    for (i = 0; i < linecnt; i++) {
        if (lineNumbers[i] == linenum) {
            // Delete line if line is NULL or empty
            if (line == NULL || line[0] == '\0') {
                // Shift lines up
                for (int j = i; j < linecnt - 1; j++) {
                    lines[j] = lines[j + 1];
                    lineNumbers[j] = lineNumbers[j + 1];
                }
                linecnt--;
                return;
            }
            // Replace line content
            lines[i] = progptr;
            strcpy(progptr, line);
            progptr += strlen(line) + 1;
            return;
        }
        if (lineNumbers[i] > linenum) {
            break;
        }
    }
    
    // Insert new line (don't insert if line is NULL or empty)
    if (line == NULL || line[0] == '\0') return;
    
    if (linecnt >= MAX_LINES) {
        MMBasic_Error(ERR_OUT_MEMORY, "Program too large");
        return;
    }
    
    // Shift lines down
    for (int j = linecnt; j > i; j--) {
        lines[j] = lines[j - 1];
        lineNumbers[j] = lineNumbers[j - 1];
    }
    
    // Store new line
    lines[i] = progptr;
    lineNumbers[i] = linenum;
    strcpy(progptr, line);
    progptr += strlen(line) + 1;
    linecnt++;
}

// RUN command
extern int onErrorSkipActive;

void MMBasic_RunProgram(void) {
    if (linecnt == 0) {
        HAL_Display_Println("No program");
        return;
    }
    
    // Reset stacks
    forstackptr = 0;
    gosubstackptr = 0;
    whilestackptr = 0;
    dostackptr = 0;
    shadowBaseSP = 0;
    shadowPtr = 0;
    subFunCallReturnIdx = -1;
    staticVarCount = 0;
    currentSubFunIdx = -1;
    subFunIdxSP = 0;
    onErrorSkipActive = 0;
    onErrorSkipCount = 0;
    
    // Scan for SUB/FUNCTION definitions
    ScanSubFunDefs();
    
    // Execute lines with flow control
    currentLineIndex = 0;
    while (currentLineIndex >= 0 && currentLineIndex < linecnt && !MMAbort) {
        currentLine = lines[currentLineIndex];
        flowControlActive = false;
        
        // TRACE: print line number
        if (traceOn) {
            char tb[16]; sprintf(tb, "[%d] ", lineNumbers[currentLineIndex]); HAL_Display_Print(tb);
        }
        
        // Set up error recovery
        int jmpResult = setjmp(mark);
        if (jmpResult == 0) {
            MMBasic_Execute(currentLine);
        } else if (jmpResult == 2) {
            // ON ERROR SKIP triggered - skip N lines
            int skip = onErrorSkipActive;
            onErrorSkipActive = 0;
            currentLineIndex += skip;
            flowControlActive = true;
            continue;
        } else {
            // Error occurred (jmpResult == 1)
            HAL_Display_Print(" in line ");
            char buf[16];
            sprintf(buf, "%d", lineNumbers[currentLineIndex]);
            HAL_Display_Println(buf);
            return;
        }
        
        // Advance unless a command changed the flow (GOTO, NEXT loop, etc.)
        if (!flowControlActive) {
            currentLineIndex++;
        }
    }
    
    MMAbort = 0;
}

// LIST command
void MMBasic_ListProgram(void) {
    if (linecnt == 0) {
        HAL_Display_Println("No program");
        return;
    }
    
    for (int i = 0; i < linecnt; i++) {
        char buf[16];
        sprintf(buf, "%d ", lineNumbers[i]);
        HAL_Display_Print(buf);
        HAL_Display_Print(lines[i]);
        HAL_Display_Newline();
    }
}

// Save program to file
void MMBasic_SaveProgram(char *filename) {
    if (!HAL_SD_Init()) {
        HAL_Display_Println("SD card not available");
        return;
    }
    
    // Ensure filename has leading /
    char fullPath[FILENAME_LENGTH + 2];
    if (filename[0] != '/') {
        fullPath[0] = '/';
        strncpy(fullPath + 1, filename, FILENAME_LENGTH - 1);
        fullPath[FILENAME_LENGTH] = '\0';
    } else {
        strncpy(fullPath, filename, FILENAME_LENGTH);
        fullPath[FILENAME_LENGTH] = '\0';
    }
    
    File file = HAL_SD_Open(fullPath, "w");
    if (!file) {
        HAL_Display_Println("Cannot create file");
        return;
    }
    
    // Write each line
    for (int i = 0; i < linecnt; i++) {
        char buf[16];
        sprintf(buf, "%d ", lineNumbers[i]);
        file.print(buf);
        file.println(lines[i]);
    }
    
    file.close();
    HAL_Display_Println("Program saved");
}

// Load program from file
void MMBasic_LoadProgram(char *filename) {
    if (!HAL_SD_Init()) {
        HAL_Display_Println("SD card not available");
        return;
    }
    
    // Ensure filename has leading /
    char fullPath[FILENAME_LENGTH + 2];
    if (filename[0] != '/') {
        fullPath[0] = '/';
        strncpy(fullPath + 1, filename, FILENAME_LENGTH - 1);
        fullPath[FILENAME_LENGTH] = '\0';
    } else {
        strncpy(fullPath, filename, FILENAME_LENGTH);
        fullPath[FILENAME_LENGTH] = '\0';
    }
    
    File file = HAL_SD_Open(fullPath, "r");
    if (!file) {
        HAL_Display_Println("File not found");
        return;
    }
    
    // Clear current program
    MMBasic_Reset();
    
    // Read lines
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        
        if (line.length() > 0) {
            // Parse line number
            int spaceIdx = line.indexOf(' ');
            if (spaceIdx > 0) {
                String numStr = line.substring(0, spaceIdx);
                int linenum = numStr.toInt();
                String code = line.substring(spaceIdx + 1);
                
                // Store line
                char codeBuf[STRINGSIZE];
                code.toCharArray(codeBuf, STRINGSIZE);
                MMBasic_StoreLine(linenum, codeBuf);
            }
        }
    }
    
    file.close();
    HAL_Display_Println("Program loaded");
}

// ============================================================================
// Graphics Commands
// ============================================================================

// COLOR command - set foreground and background color
void MMBasic_CmdColor(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Get foreground color
    int fgType, fgIval;
    float fgFval;
    char *fgSval;
    if (MMBasic_EvaluateExpression(&line, &fgType, &fgIval, &fgFval, &fgSval)) return;
    
    uint16_t fgColor = (uint16_t)fgIval;
    uint16_t bg = BLACK;
    
    // Check for comma (background color)
    while (*line == ' ') line++;
    if (*line == ',') {
        line++;
        int bgType, bgIval;
        float bgFval;
        char *bgSval;
        if (MMBasic_EvaluateExpression(&line, &bgType, &bgIval, &bgFval, &bgSval)) return;
        bg = (uint16_t)bgIval;
    }
    
    HAL_Display_SetTextColor(fgColor);
    HAL_Display_SetBgColor(bg);
}

// LOCATE command - set cursor position
void MMBasic_CmdLocate(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Get X position
    int xType, xIval;
    float xFval;
    char *xSval;
    if (MMBasic_EvaluateExpression(&line, &xType, &xIval, &xFval, &xSval)) return;
    
    // Expect comma
    while (*line == ' ') line++;
    if (*line != ',') {
        MMBasic_Error(ERR_SYNTAX, "Expected ,");
        return;
    }
    line++;
    
    // Get Y position
    int yType, yIval;
    float yFval;
    char *ySval;
    if (MMBasic_EvaluateExpression(&line, &yType, &yIval, &yFval, &ySval)) return;
    
    M5Cardputer.Display.setCursor(xIval, yIval);
}

// LINE command - draw line
// LINE x1, y1, x2, y2 [, color]
void MMBasic_CmdLine(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    // Check for LINE INPUT
    if ((line[0]=='I'||line[0]=='i') && (line[1]=='N'||line[1]=='n') &&
        (line[2]=='P'||line[2]=='p') && (line[3]=='U'||line[3]=='u') &&
        (line[4]=='T'||line[4]=='t')) {
        line += 5;
        // LINE INPUT handling
        int fnbr = 0;
        while (*line == ' ') line++;
        if (*line == '#') { line++; while (*line==' ') line++;
            int t2, n; float f2; char *s2;
            if (MMBasic_EvaluateExpression(&line,&t2,&n,&f2,&s2)) return;
            fnbr = n;
            while (*line==' ') line++; if (*line==',') line++;
        }
        char prompt[STRINGSIZE] = "";
        if (*line == '"') { line++; int i=0;
            while (*line!='"'&&*line&&i<STRINGSIZE-1) prompt[i++]=*line++; prompt[i]=0;
            if (*line=='"') line++;
            while (*line==' ') line++; if (*line==';') line++;
        }
        while (*line == ' ') line++;
        char varName[MAXVARLEN+2]; int i=0;
        while ((*line>='A'&&*line<='Z')||(*line>='a'&&*line<='z')||*line=='_'||*line=='$') {
            if (i<MAXVARLEN+1) varName[i++]=*line; line++;
        } varName[i]=0;
        for (int j=0;varName[j];j++) if (varName[j]>='a'&&varName[j]<='z') varName[j]-=32;
        if (varName[0]==0) { MMBasic_Error(ERR_SYNTAX,"Expected string variable"); return; }
        
        char input[STRINGSIZE];
        if (fnbr) {
            // LINE INPUT #fnbr - read full line from file
            int bi = 0; char ch;
            while (fileTable[fnbr].file.available() && bi < STRINGSIZE-1) {
                ch = fileTable[fnbr].file.read();
                if (ch == '\n' || ch == '\r') break;
                input[bi++] = ch;
            }
            input[bi] = 0;
        } else {
            if (prompt[0]) HAL_Display_Print(prompt);
            HAL_Keyboard_GetLine(input, STRINGSIZE);
        }
        int idx = MMBasic_FindVariable(varName);
        if (idx<0) idx = MMBasic_CreateVariable(varName, T_STR);
        if (idx>=0 && vartbl[idx].type==T_STR)
            strncpy(vartbl[idx].val.sval, input, STRINGSIZE-1);
        return;
    }
    
    // Normal LINE drawing
    // Get x1
    int x1Type, x1Ival;
    float x1Fval;
    char *x1Sval;
    if (MMBasic_EvaluateExpression(&line, &x1Type, &x1Ival, &x1Fval, &x1Sval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get y1
    int y1Type, y1Ival;
    float y1Fval;
    char *y1Sval;
    if (MMBasic_EvaluateExpression(&line, &y1Type, &y1Ival, &y1Fval, &y1Sval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get x2
    int x2Type, x2Ival;
    float x2Fval;
    char *x2Sval;
    if (MMBasic_EvaluateExpression(&line, &x2Type, &x2Ival, &x2Fval, &x2Sval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get y2
    int y2Type, y2Ival;
    float y2Fval;
    char *y2Sval;
    if (MMBasic_EvaluateExpression(&line, &y2Type, &y2Ival, &y2Fval, &y2Sval)) return;
    
    // Get optional color
    uint16_t color = WHITE;
    while (*line == ' ') line++;
    if (*line == ',') {
        line++;
        int cType, cIval;
        float cFval;
        char *cSval;
        if (MMBasic_EvaluateExpression(&line, &cType, &cIval, &cFval, &cSval)) return;
        color = (uint16_t)cIval;
    }
    
    M5Cardputer.Display.drawLine(x1Ival, y1Ival, x2Ival, y2Ival, color);
}

// CIRCLE command - draw circle
// CIRCLE x, y, r [, color]
void MMBasic_CmdCircle(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Get x
    int xType, xIval;
    float xFval;
    char *xSval;
    if (MMBasic_EvaluateExpression(&line, &xType, &xIval, &xFval, &xSval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get y
    int yType, yIval;
    float yFval;
    char *ySval;
    if (MMBasic_EvaluateExpression(&line, &yType, &yIval, &yFval, &ySval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get radius
    int rType, rIval;
    float rFval;
    char *rSval;
    if (MMBasic_EvaluateExpression(&line, &rType, &rIval, &rFval, &rSval)) return;
    
    // Get optional color
    uint16_t color = WHITE;
    while (*line == ' ') line++;
    if (*line == ',') {
        line++;
        int cType, cIval;
        float cFval;
        char *cSval;
        if (MMBasic_EvaluateExpression(&line, &cType, &cIval, &cFval, &cSval)) return;
        color = (uint16_t)cIval;
    }
    
    M5Cardputer.Display.drawCircle(xIval, yIval, rIval, color);
}

// RECT command - draw rectangle
// RECT x, y, w, h [, color]
void MMBasic_CmdRect(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Get x
    int xType, xIval;
    float xFval;
    char *xSval;
    if (MMBasic_EvaluateExpression(&line, &xType, &xIval, &xFval, &xSval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get y
    int yType, yIval;
    float yFval;
    char *ySval;
    if (MMBasic_EvaluateExpression(&line, &yType, &yIval, &yFval, &ySval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get width
    int wType, wIval;
    float wFval;
    char *wSval;
    if (MMBasic_EvaluateExpression(&line, &wType, &wIval, &wFval, &wSval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get height
    int hType, hIval;
    float hFval;
    char *hSval;
    if (MMBasic_EvaluateExpression(&line, &hType, &hIval, &hFval, &hSval)) return;
    
    // Get optional color
    uint16_t color = WHITE;
    while (*line == ' ') line++;
    if (*line == ',') {
        line++;
        int cType, cIval;
        float cFval;
        char *cSval;
        if (MMBasic_EvaluateExpression(&line, &cType, &cIval, &cFval, &cSval)) return;
        color = (uint16_t)cIval;
    }
    
    M5Cardputer.Display.drawRect(xIval, yIval, wIval, hIval, color);
}

// PIXEL command - draw pixel
// PIXEL x, y [, color]
void MMBasic_CmdPixel(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Get x
    int xType, xIval;
    float xFval;
    char *xSval;
    if (MMBasic_EvaluateExpression(&line, &xType, &xIval, &xFval, &xSval)) return;
    
    while (*line == ' ') line++;
    if (*line != ',') { MMBasic_Error(ERR_SYNTAX, "Expected ,"); return; }
    line++;
    
    // Get y
    int yType, yIval;
    float yFval;
    char *ySval;
    if (MMBasic_EvaluateExpression(&line, &yType, &yIval, &yFval, &ySval)) return;
    
    // Get optional color
    uint16_t color = WHITE;
    while (*line == ' ') line++;
    if (*line == ',') {
        line++;
        int cType, cIval;
        float cFval;
        char *cSval;
        if (MMBasic_EvaluateExpression(&line, &cType, &cIval, &cFval, &cSval)) return;
        color = (uint16_t)cIval;
    }
    
    M5Cardputer.Display.drawPixel(xIval, yIval, color);
}

// ============================================================================
// Multi-line IF/ELSE/ENDIF
// ============================================================================

// ============================================================================
// Multi-line IF/ELSE/ENDIF flow control
// ============================================================================

// Check if a line starts with a given keyword (case-insensitive)
static bool LineStartsWith(const char* line, const char* keyword) {
    while (*line == ' ' || *line == '\t') line++;
    int klen = strlen(keyword);
    for (int i = 0; i < klen; i++) {
        char c = line[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != keyword[i]) return false;
    }
    // Make sure it's a full keyword (followed by space, EOL, or nothing)
    char after = line[klen];
    return after == ' ' || after == '\t' || after == '\0' || after == '\n' || after == '\r';
}

// Skip forward to matching ELSE or ENDIF, handling nested IF blocks.
// Updates currentLineIndex. Call when IF condition was false (skip to ELSE)
// or when IF was true and we hit ELSE (skip to ENDIF).
void SkipToElseOrEndif(bool stopAtElse) {
    int depth = 1;
    currentLineIndex++;
    flowControlActive = true;
    while (currentLineIndex < linecnt) {
        const char* l = lines[currentLineIndex];
        if (LineStartsWith(l, "IF")) {
            depth++;
        } else if (depth == 1 && stopAtElse && LineStartsWith(l, "ELSE")) {
            return; // Found ELSE at our level, execute from here
        } else if (LineStartsWith(l, "ENDIF")) {
            depth--;
            if (depth == 0) {
                return; // Found matching ENDIF, continue after it
            }
        } else if (depth == 1 && LineStartsWith(l, "ELSE")) {
            // Hit ELSE while not looking for it (shouldn't happen in well-formed code)
            depth--;
            if (depth == 0) return;
        }
        currentLineIndex++;
    }
}

// Enhanced IF command with multiline support
void MMBasic_CmdIfEnhanced(void) {
    char *line = currentLine;
    
    // Evaluate condition
    int itype, ival;
    float fval;
    char *sval;
    
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) {
        return; // Error
    }
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Check for THEN (case-insensitive)
    if ((line[0] == 'T' || line[0] == 't') && (line[1] == 'H' || line[1] == 'h') &&
        (line[2] == 'E' || line[2] == 'e') && (line[3] == 'N' || line[3] == 'n')) {
        line += 4;
        while (*line == ' ') line++;
        
        // Check if there's code after THEN on the same line
        if (*line != '\0' && *line != '\n' && *line != '\r') {
            // Single line IF...THEN...ELSE statement
            if (ival) {
                MMBasic_Execute(line);
            }
        } else {
            // Multiline IF...THEN
            if (!ival) {
                // Condition is false, skip to ELSE or ENDIF
                SkipToElseOrEndif(true);
            }
            // If true, just continue to next line normally
        }
    } else {
        // Simple IF without THEN
        if (ival) {
            MMBasic_Execute(line);
        }
    }
}

// ELSE command
void MMBasic_CmdElse(void) {
    // We reached ELSE, which means the IF part was true (we didn't skip)
    // So we need to skip to ENDIF
    SkipToElseOrEndif(false);
}

// ENDIF command
void MMBasic_CmdEndif(void) {
    // Nothing to do, just continue to next line
}

// ============================================================================
// Array Support
// ============================================================================

// DIM command - declare array (supports up to 4 dimensions)
// DIM A(10)        -> 1D, 11 elements (int)
// DIM A(10, 10)    -> 2D, 11x11 = 121 elements
// DIM A!(10)       -> 1D float array
// DIM A$(10)       -> 1D string array
void MMBasic_CmdDim(void) {
    char *line = currentLine;

    // Skip whitespace
    while (*line == ' ') line++;

    // Get variable name (including type suffix !, %, $)
    char varName[MAXVARLEN + 2];
    int i = 0;
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') ||
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
           *line == '!' || *line == '%') {
        if (i < MAXVARLEN + 1) varName[i++] = *line;
        line++;
    }
    varName[i] = '\0';

    // Convert to uppercase (but preserve suffix characters)
    for (int j = 0; varName[j]; j++) {
        if (varName[j] >= 'a' && varName[j] <= 'z') {
            varName[j] = varName[j] - 'a' + 'A';
        }
    }

    // Expect opening parenthesis
    while (*line == ' ') line++;
    if (*line != '(') {
        MMBasic_Error(ERR_SYNTAX, "Expected (");
        return;
    }
    line++;

    // Parse up to MAXDIMS dimensions separated by commas
    int dims[MAXDIMS] = {0};
    int ndims = 0;

    while (ndims < MAXDIMS) {
        int sizeType, sizeIval;
        float sizeFval;
        char *sizeSval;
        if (MMBasic_EvaluateExpression(&line, &sizeType, &sizeIval, &sizeFval, &sizeSval)) return;
        dims[ndims++] = sizeIval;

        while (*line == ' ') line++;
        if (*line == ',') {
            line++;
            while (*line == ' ') line++;
        } else {
            break;
        }
    }

    // Expect closing parenthesis
    while (*line == ' ') line++;
    if (*line != ')') {
        MMBasic_Error(ERR_SYNTAX, "Expected )");
        return;
    }
    line++;

    // Determine variable type from suffix
    char type = MMBasic_GetVarType(varName);

    // Create variable as array
    int varIdx = MMBasic_FindVariable(varName);
    if (varIdx < 0) {
        varIdx = MMBasic_CreateVariable(varName, type);
        if (varIdx < 0) return;
    }

    // Calculate total size: (d0+1) * (d1+1) * ... * (dn+1)
    int totalSize = 1;
    for (int d = 0; d < ndims; d++) {
        totalSize *= (dims[d] + 1);
    }

    // Store dimension info
    vartbl[varIdx].ndims = ndims;
    for (int d = 0; d < ndims; d++) {
        vartbl[varIdx].dims[d] = dims[d];
    }
    for (int d = ndims; d < MAXDIMS; d++) {
        vartbl[varIdx].dims[d] = 0;
    }

    // Allocate array memory based on type
    int elemSize = sizeof(int);  // Default for T_INT
    if (type == T_FLOAT) elemSize = sizeof(float);
    else if (type == T_STR) elemSize = sizeof(char*);  // Pointer to string

    vartbl[varIdx].arr = (int *)malloc(totalSize * elemSize);
    if (vartbl[varIdx].arr == NULL) {
        MMBasic_Error(ERR_OUT_MEMORY, "Cannot allocate array");
        return;
    }

    // Initialize array to zero
    memset(vartbl[varIdx].arr, 0, totalSize * elemSize);
}

// ============================================================================
// File I/O Commands
// ============================================================================

// OPEN command
// OPEN filename FOR mode AS #filenumber
void MMBasic_CmdOpen(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    // Get filename
    char filename[FILENAME_LENGTH];
    if (*line == '"') {
        line++; int i=0;
        while (*line!='"'&&*line&&i<FILENAME_LENGTH-1) filename[i++]=*line++; filename[i]=0;
        if (*line=='"') line++;
    } else { int i=0; while (*line!=' '&&*line&&i<FILENAME_LENGTH-1) filename[i++]=*line++; filename[i]=0; }
    
    // Check for COM port (PicoMite: OPEN "COM1: speed" AS #fnbr)
    if ((filename[0]=='C'||filename[0]=='c')&&(filename[1]=='O'||filename[1]=='o')&&
        (filename[2]=='M'||filename[2]=='m')) {
        int cn = filename[3]-'0';
        if (cn<1||cn>2) { MMBasic_Error(ERR_FILE_IO,"Only COM1:/COM2: supported"); return; }
        while (*line==' ') line++;
        if (*line==',') line++;  // skip comma after port name
        // Get baud rate
        int t2, baud; float f2; char *s2;
        if (MMBasic_EvaluateExpression(&line,&t2,&baud,&f2,&s2)) return;
        // Get AS and file number
        while (*line==' ') line++;
        if ((line[0]=='A'||line[0]=='a')&&(line[1]=='S'||line[1]=='s')) line+=2;
        while (*line==' ') line++;
        if (*line=='#') line++;
        int t3, fnbr; float f3; char *s3;
        if (MMBasic_EvaluateExpression(&line,&t3,&fnbr,&f3,&s3)) return;
        if (fnbr<1||fnbr>MAXOPENFILES) { MMBasic_Error(ERR_FILE_IO,"Invalid file number"); return; }
        if (fileTable[fnbr].inUse) { MMBasic_Error(ERR_FILE_IO,"File number already open"); return; }
        HardwareSerial *ser = (cn==1) ? &Serial1 : &Serial2;
        ser->begin(baud);
        fileTable[fnbr].isCom = true;
        fileTable[fnbr].comPort = cn;
        fileTable[fnbr].inUse = true;
        return;
    }
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Expect FOR (optional, also accept AS directly) - case-insensitive
    if ((line[0] == 'F' || line[0] == 'f') && (line[1] == 'O' || line[1] == 'o') &&
        (line[2] == 'R' || line[2] == 'r')) {
        line += 3;
    }
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Get mode
    char modeStr[16];
    {
        int i = 0;
        while (*line != ' ' && *line != '\0' && *line != ',' && i < 15) {
            modeStr[i++] = *line++;
        }
        modeStr[i] = '\0';
        
        // Convert to uppercase
        for (int j = 0; modeStr[j]; j++) {
            if (modeStr[j] >= 'a' && modeStr[j] <= 'z') {
                modeStr[j] -= 32;
            }
        }
    }
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Handle quoted mode like "INPUT"
    if (modeStr[0] == '"') {
        int j = 0;
        int i = 1;
        while (modeStr[i] != '"' && modeStr[i] != '\0' && j < 15) {
            modeStr[j++] = modeStr[i++];
        }
        modeStr[j] = '\0';
    }
    
    // Expect AS (case-insensitive)
    while (*line == ' ') line++;
    if ((line[0] == 'A' || line[0] == 'a') && (line[1] == 'S' || line[1] == 's')) {
        line += 2;
    }
    
    // Get file number
    while (*line == ' ') line++;
    if (*line == '#') line++;
    
    int numType, numIval;
    float numFval;
    char *numSval;
    if (MMBasic_EvaluateExpression(&line, &numType, &numIval, &numFval, &numSval)) return;
    
    int fnbr = numIval;
    if (fnbr < 1 || fnbr > MAXOPENFILES) {
        MMBasic_Error(ERR_FILE_IO, "Invalid file number");
        return;
    }
    
    if (fileTable[fnbr].inUse) {
        MMBasic_Error(ERR_FILE_IO, "File number already open");
        return;
    }
    
    // Map MMBasic mode to Arduino SD mode
    const char* arduinoMode;
    if (strcmp(modeStr, "INPUT") == 0) {
        arduinoMode = FILE_READ;
    } else if (strcmp(modeStr, "OUTPUT") == 0) {
        arduinoMode = FILE_WRITE;
    } else if (strcmp(modeStr, "APPEND") == 0) {
        arduinoMode = FILE_APPEND;
    } else if (strcmp(modeStr, "RANDOM") == 0) {
        arduinoMode = FILE_WRITE;  // Read+write
    } else {
        MMBasic_Error(ERR_FILE_IO, "Invalid file mode");
        return;
    }
    
    if (!HAL_SD_Init()) {
        MMBasic_Error(ERR_FILE_IO, "SD card not available");
        return;
    }
    
    // For OUTPUT mode, remove existing file to ensure clean truncation
    if (strcmp(modeStr, "OUTPUT") == 0 && SD.exists(filename)) {
        SD.remove(filename);
    }
    
    fileTable[fnbr].file = SD.open(filename, arduinoMode);
    if (!fileTable[fnbr].file) {
        MMBasic_Error(ERR_FILE_IO, "Cannot open file");
        return;
    }
    
    fileTable[fnbr].inUse = true;
}

// CLOSE command
void MMBasic_CmdClose(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Get file number
    if (*line == '#') line++;
    
    int numType, numIval;
    float numFval;
    char *numSval;
    if (MMBasic_EvaluateExpression(&line, &numType, &numIval, &numFval, &numSval)) return;
    
    int fnbr = numIval;
    if (fnbr < 1 || fnbr > MAXOPENFILES) {
        MMBasic_Error(ERR_FILE_IO, "Invalid file number");
        return;
    }
    
    if (!fileTable[fnbr].inUse) {
        MMBasic_Error(ERR_FILE_IO, "File not open");
        return;
    }
    
    if (fileTable[fnbr].isCom) {
        HardwareSerial *s = (fileTable[fnbr].comPort==1)?&Serial1:&Serial2;
        s->end();
    } else {
        fileTable[fnbr].file.close();
    }
    fileTable[fnbr].inUse = false;
    fileTable[fnbr].isCom = false;
}

// SEEK command
// SEEK #fnbr, position
void MMBasic_CmdSeek(void) {
    char *line = currentLine;
    
    // Skip whitespace
    while (*line == ' ') line++;
    
    // Get file number
    if (*line == '#') line++;
    
    int numType, numIval;
    float numFval;
    char *numSval;
    if (MMBasic_EvaluateExpression(&line, &numType, &numIval, &numFval, &numSval)) return;
    
    int fnbr = numIval;
        if (fnbr < 1 || fnbr > MAXOPENFILES || !fileTable[fnbr].inUse) {
            MMBasic_Error(ERR_FILE_IO, "File not open");
            return;
        }
        if (fileTable[fnbr].isCom) {
            // Skip comma, then output via Serial
            while (*line == ' ') line++;
            if (*line == ',') line++;
        }
    
    // Expect comma
    while (*line == ' ') line++;
    if (*line == ',') line++;
    
    if (MMBasic_EvaluateExpression(&line, &numType, &numIval, &numFval, &numSval)) return;
    
    int position = numIval;
    if (position < 0) position = 0;
    
    fileTable[fnbr].file.seek(position);
}

// POKE command - write byte to memory
// POKE addr, value
void MMBasic_CmdPoke(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    int t1, addr;
    float f1; char *s1;
    if (MMBasic_EvaluateExpression(&line, &t1, &addr, &f1, &s1)) return;
    
    while (*line == ' ') line++;
    if (*line == ',') line++;
    
    int t2, val;
    float f2; char *s2;
    if (MMBasic_EvaluateExpression(&line, &t2, &val, &f2, &s2)) return;
    
    *((unsigned char *)addr) = (unsigned char)(val & 0xFF);
}
// SWAP var1, var2
void MMBasic_CmdSwap(void) {
    char *line = currentLine;
    
    // Get first variable (including type suffix !, %, $)
    while (*line == ' ') line++;
    char name1[MAXVARLEN + 2];
    int i = 0;
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
           *line == '!' || *line == '%') {
        if (i < MAXVARLEN + 1) name1[i++] = *line;
        line++;
    }
    name1[i] = '\0';
    for (int j = 0; name1[j]; j++) if (name1[j] >= 'a' && name1[j] <= 'z') name1[j] -= 32;
    
    // Skip comma
    while (*line == ' ') line++;
    if (*line == ',') line++;
    
    // Get second variable (including type suffix !, %, $)
    while (*line == ' ') line++;
    char name2[MAXVARLEN + 2];
    i = 0;
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
           *line == '!' || *line == '%') {
        if (i < MAXVARLEN + 1) name2[i++] = *line;
        line++;
    }
    name2[i] = '\0';
    for (int j = 0; name2[j]; j++) if (name2[j] >= 'a' && name2[j] <= 'z') name2[j] -= 32;
    
    // Ensure both variables exist
    int idx1 = MMBasic_FindVariable(name1);
    int idx2 = MMBasic_FindVariable(name2);
    if (idx1 < 0) idx1 = MMBasic_CreateVariable(name1, MMBasic_GetVarType(name1));
    if (idx2 < 0) idx2 = MMBasic_CreateVariable(name2, MMBasic_GetVarType(name2));
    if (idx1 < 0 || idx2 < 0) return;
    
    // Swap the entire variable struct
    struct s_vartbl temp = vartbl[idx1];
    vartbl[idx1] = vartbl[idx2];
    vartbl[idx2] = temp;
    // Swap back the names to keep array vars with correct names
    char tmpname[MAXVARLEN + 1];
    strcpy(tmpname, vartbl[idx2].name);
    strcpy(vartbl[idx2].name, vartbl[idx1].name);
    strcpy(vartbl[idx1].name, tmpname);
}

// INC command - increment variable
// INC varname [, amount]
void MMBasic_CmdInc(void) {
    char *line = currentLine;
    
    while (*line == ' ') line++;
    char varName[MAXVARLEN + 2];
    int i = 0;
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
           *line == '!' || *line == '%') {
        if (i < MAXVARLEN + 1) varName[i++] = *line;
        line++;
    }
    varName[i] = '\0';
    for (int j = 0; varName[j]; j++) if (varName[j] >= 'a' && varName[j] <= 'z') varName[j] -= 32;
    
    // Get optional amount
    int amount = 1;
    while (*line == ' ') line++;
    if (*line == ',') {
        line++;
        int t; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &t, &amount, &f, &s)) return;
    }
    
    int idx = MMBasic_FindVariable(varName);
    if (idx < 0) idx = MMBasic_CreateVariable(varName, MMBasic_GetVarType(varName));
    if (idx < 0) return;
    
    if (vartbl[idx].type == T_FLOAT)
        vartbl[idx].val.fval += (float)amount;
    else
        vartbl[idx].val.ival += amount;
}

// PAUSE command - pause execution
// PAUSE ms
void MMBasic_CmdPause(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    int itype, ms;
    float fval; char *sval;
    if (MMBasic_EvaluateExpression(&line, &itype, &ms, &fval, &sval)) return;
    HAL_Delay(ms > 0 ? ms : 0);
}

// RANDOMIZE command - seed random number generator
// RANDOMIZE [seed]
void MMBasic_CmdRandomize(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    if (*line == '\0' || *line == '\n' || *line == '\r') {
        srand(millis());
    } else {
        int itype, seed;
        float fval; char *sval;
        if (MMBasic_EvaluateExpression(&line, &itype, &seed, &fval, &sval)) return;
        srand(seed);
    }
}

// CONST command - define a named constant
// CONST name = value
void MMBasic_CmdConst(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    // Get variable name (including type suffix !, %, $)
    char name[MAXVARLEN + 2];
    int i = 0;
    while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
           (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
           *line == '!' || *line == '%') {
        if (i < MAXVARLEN + 1) name[i++] = *line;
        line++;
    }
    name[i] = '\0';
    for (int j = 0; name[j]; j++) if (name[j] >= 'a' && name[j] <= 'z') name[j] -= 32;
    
    while (*line == ' ') line++;
    if (*line != '=') { MMBasic_Error(ERR_SYNTAX, "Expected ="); return; }
    line++;
    
    int itype, ival; float fval; char *sval;
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
    
    // Use type from suffix or expression
    char vtype = MMBasic_GetVarType(name);
    if (vtype == T_INT && itype == T_FLOAT) vtype = T_FLOAT;
    if (vtype == T_INT && itype == T_STR) vtype = T_STR;
    int idx = MMBasic_CreateVariable(name, vtype);
    if (idx < 0) return;
    MMBasic_SetVariable(idx, ival, fval, sval);
    varIsConst[idx] = true;
}

// READ command - read from DATA statements
// READ var1, var2$, ...
void MMBasic_CmdRead(void) {
    char *line = currentLine;
    
    while (*line != '\0' && *line != '\n' && *line != '\r') {
        while (*line == ' ') line++;
        if (*line == '\0' || *line == '\n' || *line == '\r') break;
        
        // Get variable name (including type suffix !, %, $)
        char varName[MAXVARLEN + 2];
        int i = 0;
        while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') || 
               (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
               *line == '!' || *line == '%') {
            if (i < MAXVARLEN + 1) varName[i++] = *line;
            line++;
        }
        varName[i] = '\0';
        for (int j = 0; varName[j]; j++) if (varName[j] >= 'a' && varName[j] <= 'z') varName[j] -= 32;
        
        while (*line == ' ') line++;
        if (*line == ',') line++;
        if (varName[0] == '\0') continue;
        
        // Find next DATA value
        while (dataLineIdx < linecnt) {
            const char *dl = lines[dataLineIdx];
            while (*dl == ' ' || *dl == '\t') dl++;
            if (dl[0] == 'D' && dl[1] == 'A' && dl[2] == 'T' && dl[3] == 'A' &&
                (dl[4] == ' ' || dl[4] == '\t' || dl[4] == '\0')) {
                dl += 4;
                while (*dl == ' ' || *dl == '\t') dl++;
                char val[STRINGSIZE];
                int vi = 0;
                const char *sv = dl + dataOffset;
                while (*sv == ' ' || *sv == '\t') sv++;
                if (*sv == '\0') { dataLineIdx++; dataOffset = 0; continue; }
                if (*sv == '"') {
                    sv++;
                    while (*sv != '"' && *sv != '\0' && vi < STRINGSIZE - 1) val[vi++] = *sv++;
                    if (*sv == '"') sv++;
                } else {
                    while (*sv != ',' && *sv != '\0' && vi < STRINGSIZE - 1)
                        val[vi++] = *sv++;
                }
                while (vi > 0 && val[vi-1] == ' ') vi--;
                val[vi] = '\0';
                dataOffset = (int)(sv - dl);
                if (*sv == ',') { dataOffset++; }
                else { dataLineIdx++; dataOffset = 0; }
                
                int iv = MMBasic_FindVariable(varName);
                if (iv < 0) {
                    char t = MMBasic_GetVarType(varName);
                    iv = MMBasic_CreateVariable(varName, t);
                }
                if (iv >= 0 && !varIsConst[iv]) {
                    if (vartbl[iv].type == T_STR)
                        strncpy(vartbl[iv].val.sval, val, STRINGSIZE - 1);
                    else if (vartbl[iv].type == T_FLOAT)
                        vartbl[iv].val.fval = atof(val);
                    else
                        vartbl[iv].val.ival = atoi(val);
                }
                goto next_var;
            }
            dataLineIdx++;
        }
        MMBasic_Error(ERR_SYNTAX, "No DATA to read");
        return;
        next_var: ;
    }
}

// DO command - DO [WHILE|UNTIL condition] ... LOOP [WHILE|UNTIL condition]
void MMBasic_CmdDo(void) {
    // Check if re-entry (LOOP jumped back)
    int isReentry = (dostackptr > 0 && dostack[dostackptr - 1] == currentLineIndex);
    
    char *line = currentLine;
    while (*line == ' ') line++;
    
    int preWhile = 0, preUntil = 0;
    if ((line[0]=='W'||line[0]=='w') && (line[1]=='H'||line[1]=='h') &&
        (line[2]=='I'||line[2]=='i') && (line[3]=='L'||line[3]=='l') &&
        (line[4]=='E'||line[4]=='e')) {
        preWhile = 1; line += 5;
    } else if ((line[0]=='U'||line[0]=='u') && (line[1]=='N'||line[1]=='n') &&
               (line[2]=='T'||line[2]=='t') && (line[3]=='I'||line[3]=='i') &&
               (line[4]=='L'||line[4]=='l')) {
        preUntil = 1; line += 5;
    }
    
    int itype, ival; float fval; char *sval;
    if (preWhile || preUntil) {
        if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
        if ((preWhile && !ival) || (preUntil && ival)) {
            // Condition false - exit loop
            if (isReentry) dostackptr--;
            int depth = 1;
            currentLineIndex++;
            while (currentLineIndex < linecnt) {
                const char *l = lines[currentLineIndex];
                while (*l == ' ' || *l == '\t') l++;
                if ((l[0]=='D'||l[0]=='d') && (l[1]=='O'||l[1]=='o')) {
                    while (*l && *l != ' ' && *l != '\t') l++;
                    depth++;
                } else if ((l[0]=='L'||l[0]=='l') && (l[1]=='O'||l[1]=='o') &&
                           (l[2]=='O'||l[2]=='o') && (l[3]=='P'||l[3]=='p')) {
                    depth--;
                    if (depth == 0) { currentLineIndex++; break; }
                }
                currentLineIndex++;
            }
            flowControlActive = true;
            return;
        }
    }
    
    // Push DO position on first entry only
    if (!isReentry) {
        if (dostackptr >= MAXFORLOOPS) { MMBasic_Error(ERR_STACK_FULL, "DO stack full"); return; }
        dostack[dostackptr++] = currentLineIndex;
    }
}

// CONTINUE command - skip to next iteration of current loop
void MMBasic_CmdContinue(void) {
    if (dostackptr > 0) {
        // DO loop: jump back to DO
        currentLineIndex = dostack[dostackptr - 1];
        currentLine = lines[currentLineIndex];
        flowControlActive = true;
    } else if (whilestackptr > 0) {
        // WHILE loop: jump back to WHILE (condition will be re-evaluated)
        currentLineIndex = whilestack[whilestackptr - 1];
        currentLine = lines[currentLineIndex];
        flowControlActive = true;
    } else if (forstackptr > 0) {
        // FOR loop: scan to NEXT at same level, re-execute NEXT
        int idx = forstackptr - 1;
        currentLineIndex++;
        while (currentLineIndex < linecnt) {
            const char *l = lines[currentLineIndex];
            while (*l == ' ' || *l == '\t') l++;
            if ((l[0]=='F'||l[0]=='f') && (l[1]=='O'||l[1]=='o') && (l[2]=='R'||l[2]=='r')) {
                idx++;
            } else if ((l[0]=='N'||l[0]=='n') && (l[1]=='E'||l[1]=='e') &&
                       (l[2]=='X'||l[2]=='x') && (l[3]=='T'||l[3]=='t')) {
                if (idx == forstackptr - 1) break;
            }
            currentLineIndex++;
        }
        flowControlActive = true;
    }
}

// EXIT command - exit current loop
// EXIT [FOR|DO|WHILE]
void MMBasic_CmdExit(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    if ((line[0]=='F'||line[0]=='f') && (line[1]=='O'||line[1]=='o') && (line[2]=='R'||line[2]=='r')) {
        // EXIT FOR: skip to after NEXT
        if (forstackptr == 0) { MMBasic_Error(ERR_SYNTAX, "EXIT FOR without FOR"); return; }
        forstackptr--;
        int depth = 1;
        currentLineIndex++;
        while (currentLineIndex < linecnt) {
            const char *l = lines[currentLineIndex];
            while (*l == ' ' || *l == '\t') l++;
            if ((l[0]=='F'||l[0]=='f') && (l[1]=='O'||l[1]=='o') && (l[2]=='R'||l[2]=='r')) depth++;
            else if ((l[0]=='N'||l[0]=='n') && (l[1]=='E'||l[1]=='e') &&
                     (l[2]=='X'||l[2]=='x') && (l[3]=='T'||l[3]=='t')) {
                depth--;
                if (depth == 0) { currentLineIndex++; break; }
            }
            currentLineIndex++;
        }
        flowControlActive = true;
    } else if ((line[0]=='D'||line[0]=='d') && (line[1]=='O'||line[1]=='o')) {
        // EXIT DO: pop DO stack, skip to after LOOP
        if (dostackptr == 0) { MMBasic_Error(ERR_SYNTAX, "EXIT DO without DO"); return; }
        dostackptr--;
        int depth = 1;
        currentLineIndex++;
        while (currentLineIndex < linecnt) {
            const char *l = lines[currentLineIndex];
            while (*l == ' ' || *l == '\t') l++;
            if ((l[0]=='D'||l[0]=='d') && (l[1]=='O'||l[1]=='o')) depth++;
            else if ((l[0]=='L'||l[0]=='l') && (l[1]=='O'||l[1]=='o') &&
                     (l[2]=='O'||l[2]=='o') && (l[3]=='P'||l[3]=='p')) {
                depth--;
                if (depth == 0) { currentLineIndex++; break; }
            }
            currentLineIndex++;
        }
        flowControlActive = true;
    } else if ((line[0]=='W'||line[0]=='w') && (line[1]=='H'||line[1]=='h')) {
        // EXIT WHILE: pop WHILE stack, skip to after WEND
        if (whilestackptr == 0) { MMBasic_Error(ERR_SYNTAX, "EXIT WHILE without WHILE"); return; }
        whilestackptr--;
        int depth = 1;
        currentLineIndex++;
        while (currentLineIndex < linecnt) {
            const char *l = lines[currentLineIndex];
            while (*l == ' ' || *l == '\t') l++;
            if ((l[0]=='W'||l[0]=='w') && (l[1]=='H'||l[1]=='h') &&
                (l[2]=='I'||l[2]=='i') && (l[3]=='L'||l[3]=='l') &&
                (l[4]=='E'||l[4]=='e')) depth++;
            else if ((l[0]=='W'||l[0]=='w') && (l[1]=='E'||l[1]=='e') &&
                     (l[2]=='N'||l[2]=='n') && (l[3]=='D'||l[3]=='d')) {
                depth--;
                if (depth == 0) { currentLineIndex++; break; }
            }
            currentLineIndex++;
        }
        flowControlActive = true;
    } else {
        // Bare EXIT (no specifier): exit innermost loop
        if (dostackptr > 0) {
            dostackptr--;
            int depth = 1;
            currentLineIndex++;
            while (currentLineIndex < linecnt) {
                const char *l = lines[currentLineIndex];
                while (*l == ' ' || *l == '\t') l++;
                if ((l[0]=='D'||l[0]=='d') && (l[1]=='O'||l[1]=='o')) depth++;
                else if ((l[0]=='L'||l[0]=='l') && (l[1]=='O'||l[1]=='o') &&
                         (l[2]=='O'||l[2]=='o') && (l[3]=='P'||l[3]=='p')) {
                    depth--;
                    if (depth == 0) { currentLineIndex++; break; }
                }
                currentLineIndex++;
            }
            flowControlActive = true;
        } else if (whilestackptr > 0) {
            whilestackptr--;
            int depth = 1;
            currentLineIndex++;
            while (currentLineIndex < linecnt) {
                const char *l = lines[currentLineIndex];
                while (*l == ' ' || *l == '\t') l++;
                if ((l[0]=='W'||l[0]=='w') && (l[1]=='H'||l[1]=='h')) depth++;
                else if ((l[0]=='W'||l[0]=='w') && (l[1]=='E'||l[1]=='e')) {
                    depth--;
                    if (depth == 0) { currentLineIndex++; break; }
                }
                currentLineIndex++;
            }
            flowControlActive = true;
        } else if (forstackptr > 0) {
            forstackptr--;
            int depth = 1;
            currentLineIndex++;
            while (currentLineIndex < linecnt) {
                const char *l = lines[currentLineIndex];
                while (*l == ' ' || *l == '\t') l++;
                if ((l[0]=='F'||l[0]=='f') && (l[1]=='O'||l[1]=='o')) depth++;
                else if ((l[0]=='N'||l[0]=='n') && (l[1]=='E'||l[1]=='e')) {
                    depth--;
                    if (depth == 0) { currentLineIndex++; break; }
                }
                currentLineIndex++;
            }
            flowControlActive = true;
        }
    }
}

// KILL command - delete a file
void MMBasic_CmdKill(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    char fname[FILENAME_LENGTH];
    if (*line == '"') { line++; int i=0; while (*line!='"'&&*line&&i<FILENAME_LENGTH-1) fname[i++]=*line++; fname[i]=0; if (*line=='"') line++; }
    else { int i=0; while (*line!=' '&&*line&&i<FILENAME_LENGTH-1) fname[i++]=*line++; fname[i]=0; }
    char full[FILENAME_LENGTH+2];
    if (fname[0]!='/') { full[0]='/'; strcpy(full+1,fname); } else strcpy(full,fname);
    if (!HAL_SD_Init()) { HAL_Display_Println("SD card not available"); return; }
    if (SD.remove(full)) HAL_Display_Println("File deleted");
    else HAL_Display_Println("Cannot delete file");
}

// MKDIR command
void MMBasic_CmdMkdir(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    char dname[FILENAME_LENGTH];
    if (*line == '"') { line++; int i=0; while (*line!='"'&&*line&&i<FILENAME_LENGTH-1) dname[i++]=*line++; dname[i]=0; if (*line=='"') line++; }
    else { int i=0; while (*line!=' '&&*line&&i<FILENAME_LENGTH-1) dname[i++]=*line++; dname[i]=0; }
    char full[FILENAME_LENGTH+2];
    if (dname[0]!='/') { full[0]='/'; strcpy(full+1,dname); } else strcpy(full,dname);
    if (!HAL_SD_Init()) { HAL_Display_Println("SD card not available"); return; }
    if (SD.mkdir(full)) HAL_Display_Println("Directory created");
    else HAL_Display_Println("Cannot create directory");
}

// RMDIR command
void MMBasic_CmdRmdir(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    char dname[FILENAME_LENGTH];
    if (*line == '"') { line++; int i=0; while (*line!='"'&&*line&&i<FILENAME_LENGTH-1) dname[i++]=*line++; dname[i]=0; if (*line=='"') line++; }
    else { int i=0; while (*line!=' '&&*line&&i<FILENAME_LENGTH-1) dname[i++]=*line++; dname[i]=0; }
    char full[FILENAME_LENGTH+2];
    if (dname[0]!='/') { full[0]='/'; strcpy(full+1,dname); } else strcpy(full,dname);
    if (!HAL_SD_Init()) { HAL_Display_Println("SD card not available"); return; }
    if (SD.rmdir(full)) HAL_Display_Println("Directory removed");
    else HAL_Display_Println("Cannot remove directory");
}

// COPY command - COPY "source" TO "dest"
void MMBasic_CmdCopy(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    char src[FILENAME_LENGTH], dst[FILENAME_LENGTH];
    if (*line == '"') { line++; int i=0; while (*line!='"'&&*line&&i<FILENAME_LENGTH-1) src[i++]=*line++; src[i]=0; if (*line=='"') line++; }
    else { int i=0; while (*line!=' '&&*line&&i<FILENAME_LENGTH-1) src[i++]=*line++; src[i]=0; }
    while (*line == ' ') line++;
    // Expect TO keyword
    if ((line[0]=='T'||line[0]=='t') && (line[1]=='O'||line[1]=='o')) line += 2;
    else { HAL_Display_Println("Expected TO"); return; }
    while (*line == ' ') line++;
    if (*line == '"') { line++; int i=0; while (*line!='"'&&*line&&i<FILENAME_LENGTH-1) dst[i++]=*line++; dst[i]=0; if (*line=='"') line++; }
    else { int i=0; while (*line!=' '&&*line&&i<FILENAME_LENGTH-1) dst[i++]=*line++; dst[i]=0; }
    char fsrc[FILENAME_LENGTH+2], fdst[FILENAME_LENGTH+2];
    if (src[0]!='/') { fsrc[0]='/'; strcpy(fsrc+1,src); } else strcpy(fsrc,src);
    if (dst[0]!='/') { fdst[0]='/'; strcpy(fdst+1,dst); } else strcpy(fdst,dst);
    if (!HAL_SD_Init()) { HAL_Display_Println("SD card not available"); return; }
    File f1 = SD.open(fsrc, FILE_READ);
    if (!f1) { HAL_Display_Println("Source file not found"); return; }
    File f2 = SD.open(fdst, FILE_WRITE);
    if (!f2) { f1.close(); HAL_Display_Println("Cannot create destination"); return; }
    char buf[256]; int n;
    while ((n = f1.read((uint8_t*)buf, 256)) > 0) f2.write((uint8_t*)buf, n);
    f1.close(); f2.close();
    HAL_Display_Println("File copied");
}

// RENAME command - copy then delete (ESP32 SD rename unreliable)
void MMBasic_CmdRename(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    char oldn[FILENAME_LENGTH], newn[FILENAME_LENGTH];
    if (*line == '"') { line++; int i=0; while (*line!='"'&&*line&&i<FILENAME_LENGTH-1) oldn[i++]=*line++; oldn[i]=0; if (*line=='"') line++; }
    else { int i=0; while (*line!=','&&*line!=' '&&*line&&i<FILENAME_LENGTH-1) oldn[i++]=*line++; oldn[i]=0; }
    while (*line == ' ' || *line == ',') line++;
    if (*line == '"') { line++; int i=0; while (*line!='"'&&*line&&i<FILENAME_LENGTH-1) newn[i++]=*line++; newn[i]=0; if (*line=='"') line++; }
    else { int i=0; while (*line!=' '&&*line&&i<FILENAME_LENGTH-1) newn[i++]=*line++; newn[i]=0; }
    char gold[FILENAME_LENGTH+2], gnew[FILENAME_LENGTH+2];
    if (oldn[0]!='/') { gold[0]='/'; strcpy(gold+1,oldn); } else strcpy(gold,oldn);
    if (newn[0]!='/') { gnew[0]='/'; strcpy(gnew+1,newn); } else strcpy(gnew,newn);
    if (!HAL_SD_Init()) { HAL_Display_Println("SD card not available"); return; }
    File f1 = SD.open(gold, FILE_READ);
    if (!f1) { HAL_Display_Println("Source file not found"); return; }
    File f2 = SD.open(gnew, FILE_WRITE);
    if (!f2) { f1.close(); HAL_Display_Println("Cannot create destination"); return; }
    char buf[256]; int n;
    while ((n = f1.read((uint8_t*)buf, 256)) > 0) f2.write((uint8_t*)buf, n);
    f1.close(); f2.close();
    SD.remove(gold);
    HAL_Display_Println("File renamed");
}

// CHDIR command
void MMBasic_CmdChdir(void) {
    HAL_Display_Println("CHDIR - stub (SD card uses flat root)");
}

// DRIVE command
void MMBasic_CmdDrive(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    if ((line[0]=='B'||line[0]=='b') && (line[1]==':'||line[1]==0)) {
        currentDrive = 1;
        HAL_Display_Println("Drive B: selected");
    } else if ((line[0]=='A'||line[0]=='a') && (line[1]==':'||line[1]==0)) {
        HAL_Display_Println("Drive A: not available (flash FS)");
    } else {
        HAL_Display_Println("Invalid drive");
    }
}
void MMBasic_CmdSetpin(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    int t, pin; float f; char *s;
    if (MMBasic_EvaluateExpression(&line, &t, &pin, &f, &s)) return;
    while (*line == ' ') line++; if (*line == ',') line++;
    int t2, cfg;
    if (MMBasic_EvaluateExpression(&line, &t2, &cfg, &f, &s)) return;
    switch (cfg) {
        case 0: pinMode(pin, INPUT); break;
        case 1: pinMode(pin, OUTPUT); digitalWrite(pin, LOW); break;
        case 2: pinMode(pin, INPUT); break;
        case 3: pinMode(pin, INPUT_PULLUP); break;
        case 4: pinMode(pin, INPUT); break;
        case 5: break;
        default: HAL_Display_Println("Invalid mode"); break;
    }
}
void MMBasic_CmdDigout(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    int t, pin; float f; char *s;
    if (MMBasic_EvaluateExpression(&line, &t, &pin, &f, &s)) return;
    while (*line == ' ') line++; if (*line == ',') line++;
    int t2, val;
    if (MMBasic_EvaluateExpression(&line, &t2, &val, &f, &s)) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, val ? HIGH : LOW);
}
void MMBasic_CmdPwm(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    int t, pin; float f; char *s;
    if (MMBasic_EvaluateExpression(&line, &t, &pin, &f, &s)) return;
    while (*line == ' ') line++; if (*line == ',') line++;
    int t2, freq;
    if (MMBasic_EvaluateExpression(&line, &t2, &freq, &f, &s)) return;
    while (*line == ' ') line++; if (*line == ',') line++;
    int t3, duty;
    if (MMBasic_EvaluateExpression(&line, &t3, &duty, &f, &s)) return;
    if (duty < 0) duty = 0; if (duty > 100) duty = 100;
    ledcAttach(pin, freq, 8);
    ledcWrite(pin, (duty * 255) / 100);
}
// I2C command: I2C OPEN speed | I2C CLOSE | I2C SEND addr, data... | I2C RECEIVE addr, count
static bool i2cOpen = false;
void MMBasic_CmdI2c(void) {
    char *line = currentLine; while (*line == ' ') line++;
    char sub[16]; int i = 0;
    while (*line!=' '&&*line!='\0'&&i<15) sub[i++]=*line++; sub[i]=0;
    for (int j=0;sub[j];j++) if (sub[j]>='a'&&sub[j]<='z') sub[j]-=32;
    while (*line==' ') line++;
    if (strcmp(sub,"OPEN")==0) {
        int t, speed; float f; char *s;
        if (MMBasic_EvaluateExpression(&line,&t,&speed,&f,&s)) return;
        Wire.begin(); Wire.setClock(speed*1000); i2cOpen = true;
    } else if (strcmp(sub,"CLOSE")==0) {
        Wire.end(); i2cOpen = false;
    } else if (strcmp(sub,"SEND")==0) {
        if (!i2cOpen) { HAL_Display_Println("I2C not open"); return; }
        int t, addr; float f; char *s;
        if (MMBasic_EvaluateExpression(&line,&t,&addr,&f,&s)) return;
        while (*line==','||*line==' ') line++;
        Wire.beginTransmission(addr);
        while (*line) {
            int v; float f2; char *s2;
            if (!((*line>='0'&&*line<='9')||*line=='-')) break;
            if (MMBasic_EvaluateExpression(&line,&t,&v,&f2,&s2)) return;
            Wire.write(v & 0xFF);
            while (*line==','||*line==' ') line++;
        }
        Wire.endTransmission();
    } else if (strcmp(sub,"RECEIVE")==0) {
        if (!i2cOpen) { HAL_Display_Println("I2C not open"); return; }
        int t, addr; float f; char *s;
        if (MMBasic_EvaluateExpression(&line,&t,&addr,&f,&s)) return;
        while (*line==','||*line==' ') line++;
        int t2, count;
        if (MMBasic_EvaluateExpression(&line,&t2,&count,&f,&s)) return;
        Wire.requestFrom((uint8_t)addr,(uint8_t)count);
        while (Wire.available()&&count>0) {
            char b[8]; sprintf(b,"%02X ",Wire.read()); HAL_Display_Print(b); count--;
        }
        HAL_Display_Newline();
    } else { HAL_Display_Println("I2C: unknown sub-command"); }
}
// SPI command: SPI OPEN speed, mode, cs | SPI CLOSE | SPI READ count | SPI WRITE data...
static SPIClass *spiDev = NULL; static int spiCsPin = -1;
void MMBasic_CmdSpi(void) {
    char *line = currentLine; while (*line == ' ') line++;
    char sub[16]; int i = 0;
    while (*line!=' '&&*line!='\0'&&i<15) sub[i++]=*line++; sub[i]=0;
    for (int j=0;sub[j];j++) if (sub[j]>='a'&&sub[j]<='z') sub[j]-=32;
    while (*line==' ') line++;
    if (strcmp(sub,"OPEN")==0) {
        int t, speed; float f; char *s;
        if (MMBasic_EvaluateExpression(&line,&t,&speed,&f,&s)) return;
        while (*line==','||*line==' ') line++;
        int t2, mode;
        if (MMBasic_EvaluateExpression(&line,&t2,&mode,&f,&s)) return;
        while (*line==','||*line==' ') line++;
        int t3, cs;
        if (MMBasic_EvaluateExpression(&line,&t3,&cs,&f,&s)) return;
        spiDev = &SPI; spiCsPin = cs;
        pinMode(cs,OUTPUT); digitalWrite(cs,HIGH);
        spiDev->begin(40,39,14,cs);
        spiDev->beginTransaction(SPISettings(speed*1000000,MSBFIRST,mode));
    } else if (strcmp(sub,"CLOSE")==0) {
        if (spiDev) { spiDev->endTransaction(); spiDev->end(); spiDev=NULL; }
    } else if (strcmp(sub,"WRITE")==0) {
        if (!spiDev) { HAL_Display_Println("SPI not open"); return; }
        int tt; float ff; char *ss;
        digitalWrite(spiCsPin,LOW);
        while (*line) { while (*line==','||*line==' ') line++; int v;
            if (!((*line>='0'&&*line<='9')||*line=='-')) break;
            if (MMBasic_EvaluateExpression(&line,&tt,&v,&ff,&ss)) { digitalWrite(spiCsPin,HIGH); return; }
            spiDev->transfer(v & 0xFF); }
        digitalWrite(spiCsPin,HIGH);
    } else if (strcmp(sub,"READ")==0) {
        if (!spiDev) { HAL_Display_Println("SPI not open"); return; }
        int t, count; float f; char *s;
        if (MMBasic_EvaluateExpression(&line,&t,&count,&f,&s)) return;
        digitalWrite(spiCsPin,LOW);
        for (int i=0;i<count;i++) { char b[8]; sprintf(b,"%02X ",spiDev->transfer(0xFF)); HAL_Display_Print(b); }
        digitalWrite(spiCsPin,HIGH); HAL_Display_Newline();
    } else { HAL_Display_Println("SPI: unknown sub-command"); }
}
// Serial via OPEN: OPEN "COM1: speed" AS #fnbr
void MMBasic_CmdIr(void) {
    char *line = currentLine; while (*line == ' ') line++;
    char sub[16]; int i=0;
    while (*line!=' '&&*line!='\0'&&i<15) sub[i++]=*line++; sub[i]=0;
    for (int j=0;sub[j];j++) if (sub[j]>='a'&&sub[j]<='z') sub[j]-=32;
    while (*line==' ') line++;
    if (strcmp(sub,"SEND")==0) {
        int t, data; float f; char *s;
        if (MMBasic_EvaluateExpression(&line,&t,&data,&f,&s)) return;
        int freq = 38000;
        while (*line==','||*line==' ') line++;
        if (*line>='0'&&*line<='9') {
            int t2; float f2; char *s2;
            if (MMBasic_EvaluateExpression(&line,&t2,&freq,&f2,&s2)) return;
        }
        // Bit-bang IR at carrier freq on GPIO 44 (Cardputer IR LED)
        uint8_t irPin = 44;
        pinMode(irPin, OUTPUT);
        for (int b=0; b<8; b++) {
            if (data & (1<<b)) {
                for (int c=0; c<22; c++) { // Mark
                    digitalWrite(irPin, HIGH); delayMicroseconds(26);
                    digitalWrite(irPin, LOW); delayMicroseconds(26);
                }
                delayMicroseconds(562); // Space
            } else {
                digitalWrite(irPin, LOW);
                delayMicroseconds(562);
            }
        }
        digitalWrite(irPin, LOW);
    }
}
void MMBasic_CmdServo(void) {
    char *line = currentLine; while (*line == ' ') line++;
    int t, pin; float f; char *s;
    if (MMBasic_EvaluateExpression(&line,&t,&pin,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t2, pos;
    if (MMBasic_EvaluateExpression(&line,&t2,&pos,&f,&s)) return;
    int duty = map(pos, 0, 1000, 26, 128); // 0-1000 map to ~0.5ms-2.5ms at 50Hz
    if (duty<26) duty=26; if (duty>128) duty=128;
    ledcAttach(pin, 50, 8);
    ledcWrite(pin, duty);
}
void MMBasic_CmdPort(void) {
    char *line = currentLine; while (*line == ' ') line++;
    int t, bits; float f; char *s;
    if (MMBasic_EvaluateExpression(&line,&t,&bits,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t2, val;
    if (MMBasic_EvaluateExpression(&line,&t2,&val,&f,&s)) return;
    // Write bits to GPIO outputs via ESP32 register
    uint32_t mask = bits;
    uint32_t out = val & mask;
    GPIO.out_w1ts = out;           // Set bits
    GPIO.out_w1tc = (~out) & mask; // Clear bits
    // Set direction for used bits
    for (int i=0; i<32; i++) if (mask & (1<<i)) pinMode(i, OUTPUT);
}
void MMBasic_CmdPulse(void) {
    char *line = currentLine; while (*line == ' ') line++;
    int t, pin; float f; char *s;
    if (MMBasic_EvaluateExpression(&line,&t,&pin,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t2, width;
    if (MMBasic_EvaluateExpression(&line,&t2,&width,&f,&s)) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH); delayMicroseconds(width);
    digitalWrite(pin, LOW);
}
// BOX command: BOX x,y,w,h [,fillColor]
void MMBasic_CmdBox(void) {
    char *line = currentLine; while (*line == ' ') line++;
    int t, x; float f; char *s;
    if (MMBasic_EvaluateExpression(&line,&t,&x,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t2, y; if (MMBasic_EvaluateExpression(&line,&t2,&y,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t3, w; if (MMBasic_EvaluateExpression(&line,&t3,&w,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t4, h; if (MMBasic_EvaluateExpression(&line,&t4,&h,&f,&s)) return;
    while (*line==' ') line++;
    if (*line==',') {
        line++;
        int tc, c; if (MMBasic_EvaluateExpression(&line,&tc,&c,&f,&s)) return;
        M5Cardputer.Display.fillRect(x,y,w,h,(uint16_t)c);
    } else {
        M5Cardputer.Display.drawRect(x,y,w,h,HAL_Display_GetTextColor());
    }
}
// TEXT command: TEXT x,y,"string" [,color]
void MMBasic_CmdText(void) {
    char *line = currentLine; while (*line == ' ') line++;
    int t, x; float f; char *s;
    if (MMBasic_EvaluateExpression(&line,&t,&x,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t2, y; if (MMBasic_EvaluateExpression(&line,&t2,&y,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    char txt[256]; int i=0;
    if (*line=='"') { line++; while (*line!='"'&&*line&&i<255) txt[i++]=*line++; txt[i]=0; if (*line=='"') line++; }
    else { while (*line!=','&&*line!=' '&&*line&&i<255) txt[i++]=*line++; txt[i]=0; }
    uint16_t col = HAL_Display_GetTextColor();
    while (*line==' ') line++;
    if (*line==',') { line++; int tc,c; float f2; char *s2;
        if (MMBasic_EvaluateExpression(&line,&tc,&c,&f2,&s2)) return; col=(uint16_t)c; }
    M5Cardputer.Display.setTextColor(col);
    M5Cardputer.Display.setCursor(x,y);
    M5Cardputer.Display.print(txt);
    M5Cardputer.Display.setTextColor(HAL_Display_GetTextColor());
}
// TRIANGLE command: TRIANGLE x1,y1,x2,y2,x3,y3 [,color]
void MMBasic_CmdTriangle(void) {
    char *line = currentLine; while (*line == ' ') line++;
    int t, x1; float f; char *s;
    if (MMBasic_EvaluateExpression(&line,&t,&x1,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t2, y1; if (MMBasic_EvaluateExpression(&line,&t2,&y1,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t3, x2; if (MMBasic_EvaluateExpression(&line,&t3,&x2,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t4, y2; if (MMBasic_EvaluateExpression(&line,&t4,&y2,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t5, x3; if (MMBasic_EvaluateExpression(&line,&t5,&x3,&f,&s)) return;
    while (*line==' ') line++; if (*line==',') line++;
    int t6, y3; if (MMBasic_EvaluateExpression(&line,&t6,&y3,&f,&s)) return;
    uint16_t col = HAL_Display_GetTextColor();
    while (*line==' ') line++;
    if (*line==',') { line++; int tc,c; float f2; char *s2;
        if (MMBasic_EvaluateExpression(&line,&tc,&c,&f2,&s2)) return; col=(uint16_t)c; }
    M5Cardputer.Display.fillTriangle(x1,y1,x2,y2,x3,y3,col);
}
// CLEAR - clear all variables, keep program
void MMBasic_CmdClear(void) {
    varcnt = 0;
    memset(vartbl, 0, MAXVARS * sizeof(struct s_vartbl));
    memset(varIsConst, 0, MAXVARS * sizeof(bool));
    forstackptr = gosubstackptr = 0;
    whilestackptr = dostackptr = 0;
    selectptr = 0;
}
// ERASE array - free array memory
void MMBasic_CmdErase(void) {
    char *line = currentLine; while (*line == ' ') line++;
    char name[MAXVARLEN+2]; int i=0;
    while ((*line>='A'&&*line<='Z')||(*line>='a'&&*line<='z')||(*line>='0'&&*line<='9')||*line=='_'||*line=='$'||*line=='!'||*line=='%') {
        if (i<MAXVARLEN+1) name[i++]=*line; line++;
    } name[i]=0;
    for (int j=0;name[j];j++) if (name[j]>='a'&&name[j]<='z') name[j]-=32;
    int idx = MMBasic_FindVariable(name);
    if (idx<0||vartbl[idx].ndims==0) { HAL_Display_Println("Not an array"); return; }
    free(vartbl[idx].arr); vartbl[idx].arr=NULL; vartbl[idx].ndims=0;
    memset(vartbl[idx].dims, 0, sizeof(vartbl[idx].dims));
}
// REDIM array(newsize [, newsize2, ...])
void MMBasic_CmdRedim(void) {
    char *line = currentLine; while (*line == ' ') line++;
    char name[MAXVARLEN+2]; int i=0;
    while ((*line>='A'&&*line<='Z')||(*line>='a'&&*line<='z')||(*line>='0'&&*line<='9')||*line=='_'||*line=='$'||*line=='!'||*line=='%') {
        if (i<MAXVARLEN+1) name[i++]=*line; line++;
    } name[i]=0;
    for (int j=0;name[j];j++) if (name[j]>='a'&&name[j]<='z') name[j]-=32;
    while (*line==' ') line++;
    if (*line!='(') { MMBasic_Error(ERR_SYNTAX,"Expected ("); return; } line++;

    // Parse up to MAXDIMS dimensions
    int dims[MAXDIMS] = {0};
    int ndims = 0;
    while (ndims < MAXDIMS) {
        int t, size; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &t, &size, &f, &s)) return;
        dims[ndims++] = size;
        while (*line == ' ') line++;
        if (*line == ',') { line++; while (*line == ' ') line++; }
        else break;
    }
    while (*line == ' ') line++;
    if (*line != ')') { MMBasic_Error(ERR_SYNTAX, "Expected )"); return; }
    line++;

    // Determine type from suffix
    char type = MMBasic_GetVarType(name);
    int idx = MMBasic_FindVariable(name);
    if (idx < 0) idx = MMBasic_CreateVariable(name, type);

    // Calculate total size
    int totalSize = 1;
    for (int d = 0; d < ndims; d++) totalSize *= (dims[d] + 1);

    if (vartbl[idx].arr) free(vartbl[idx].arr);
    vartbl[idx].ndims = ndims;
    for (int d = 0; d < ndims; d++) vartbl[idx].dims[d] = dims[d];
    for (int d = ndims; d < MAXDIMS; d++) vartbl[idx].dims[d] = 0;

    // Allocate based on type
    int elemSize = sizeof(int);
    if (type == T_FLOAT) elemSize = sizeof(float);
    else if (type == T_STR) elemSize = sizeof(char*);
    vartbl[idx].arr = (int*)malloc(totalSize * elemSize);
    memset(vartbl[idx].arr, 0, totalSize * elemSize);
}
// LINE INPUT handling is inside CmdLine (checks for LINE INPUT prefix)
// ERROR command - raise custom error
void MMBasic_CmdError(void) {
    char *line = currentLine; while (*line == ' ') line++;
    if (*line == '"') { line++; char msg[STRINGSIZE]; int i=0;
        while (*line!='"'&&*line&&i<STRINGSIZE-1) msg[i++]=*line++; msg[i]=0;
        MMBasic_Error(ERR_SYNTAX, msg);
    } else {
        char msg[STRINGSIZE]; int i=0;
        while (*line&&i<STRINGSIZE-1) msg[i++]=*line++; msg[i]=0;
        MMBasic_Error(ERR_SYNTAX, msg);
    }
}

// LOCAL command - declare local variables inside SUB/FUNCTION
void MMBasic_CmdLocal(void) {
    if (shadowBaseSP == 0) {
        MMBasic_Error(ERR_SYNTAX, "LOCAL outside SUB/FUNCTION");
        return;
    }
    char *line = currentLine;
    while (*line == ' ') line++;

    while (*line && *line != '\n' && *line != '\r') {
        // Parse variable name (including type suffix !, %, $)
        char varName[MAXVARLEN + 2];
        int i = 0;
        while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') ||
               (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
               *line == '!' || *line == '%') {
            if (i < MAXVARLEN + 1) varName[i++] = *line;
            line++;
        }
        varName[i] = '\0';
        for (int j = 0; varName[j]; j++)
            if (varName[j] >= 'a' && varName[j] <= 'z') varName[j] -= 32;

        if (varName[0] == '\0') break;

        // Find or create variable
        int vi = MMBasic_FindVariable(varName);
        if (vi < 0) {
            char type = MMBasic_GetVarType(varName);
            vi = MMBasic_CreateVariable(varName, type);
            if (vi < 0) return;
        }

        // Push shadow (save current state)
        if (shadowPtr < MAX_SHADOW) {
            shadowStack[shadowPtr].varIndex = vi;
            shadowStack[shadowPtr].type = vartbl[vi].type;
            shadowStack[shadowPtr].ival = vartbl[vi].val.ival;
            shadowStack[shadowPtr].fval = vartbl[vi].val.fval;
            if (vartbl[vi].type == T_STR && vartbl[vi].val.sval) {
                shadowStack[shadowPtr].sval = (char*)malloc(STRINGSIZE);
                if (shadowStack[shadowPtr].sval)
                    strcpy(shadowStack[shadowPtr].sval, vartbl[vi].val.sval);
            } else {
                shadowStack[shadowPtr].sval = NULL;
            }
            shadowStack[shadowPtr].ndims = vartbl[vi].ndims;
            memcpy(shadowStack[shadowPtr].dims, vartbl[vi].dims, sizeof(vartbl[vi].dims));
            shadowStack[shadowPtr].arr = vartbl[vi].arr;
            shadowStack[shadowPtr].wasConst = varIsConst[vi];
            shadowStack[shadowPtr].isStatic = false;
            shadowPtr++;
        }

        // Initialize to default (zero/empty)
        vartbl[vi].val.ival = 0;
        vartbl[vi].val.fval = 0.0;
        if (vartbl[vi].type == T_STR && vartbl[vi].val.sval) {
            vartbl[vi].val.sval[0] = '\0';
        }
        vartbl[vi].ndims = 0;
        vartbl[vi].arr = NULL;
        varIsConst[vi] = false;

        // Skip comma
        while (*line == ' ') line++;
        if (*line == ',') { line++; continue; }
        break;
    }
}

// STATIC command - declare static variables inside SUB/FUNCTION
void MMBasic_CmdStatic(void) {
    if (shadowBaseSP == 0) {
        MMBasic_Error(ERR_SYNTAX, "STATIC outside SUB/FUNCTION");
        return;
    }
    char *line = currentLine;
    while (*line == ' ') line++;

    while (*line && *line != '\n' && *line != '\r') {
        // Parse variable name (including type suffix !, %, $)
        char varName[MAXVARLEN + 2];
        int i = 0;
        while ((*line >= 'A' && *line <= 'Z') || (*line >= 'a' && *line <= 'z') ||
               (*line >= '0' && *line <= '9') || *line == '_' || *line == '$' ||
               *line == '!' || *line == '%') {
            if (i < MAXVARLEN + 1) varName[i++] = *line;
            line++;
        }
        varName[i] = '\0';
        for (int j = 0; varName[j]; j++)
            if (varName[j] >= 'a' && varName[j] <= 'z') varName[j] -= 32;

        if (varName[0] == '\0') break;

        // Find or create variable
        int vi = MMBasic_FindVariable(varName);
        if (vi < 0) {
            char type = MMBasic_GetVarType(varName);
            vi = MMBasic_CreateVariable(varName, type);
            if (vi < 0) return;
        }

        // Check if this static variable has saved data from a previous call
        int si = -1;
        if (currentSubFunIdx >= 0) {
            for (int k = 0; k < staticVarCount; k++) {
                if (staticVars[k].subfunIndex == currentSubFunIdx &&
                    strcmp(staticVars[k].name, varName) == 0) {
                    si = k; break;
                }
            }
        }

        // Push shadow (save current state) with isStatic flag
        if (shadowPtr < MAX_SHADOW) {
            shadowStack[shadowPtr].varIndex = vi;
            shadowStack[shadowPtr].type = vartbl[vi].type;
            shadowStack[shadowPtr].ival = vartbl[vi].val.ival;
            shadowStack[shadowPtr].fval = vartbl[vi].val.fval;
            if (vartbl[vi].type == T_STR && vartbl[vi].val.sval) {
                shadowStack[shadowPtr].sval = (char*)malloc(STRINGSIZE);
                if (shadowStack[shadowPtr].sval)
                    strcpy(shadowStack[shadowPtr].sval, vartbl[vi].val.sval);
            } else {
                shadowStack[shadowPtr].sval = NULL;
            }
            shadowStack[shadowPtr].ndims = vartbl[vi].ndims;
            memcpy(shadowStack[shadowPtr].dims, vartbl[vi].dims, sizeof(vartbl[vi].dims));
            shadowStack[shadowPtr].arr = vartbl[vi].arr;
            shadowStack[shadowPtr].wasConst = varIsConst[vi];
            shadowStack[shadowPtr].isStatic = true;
            shadowPtr++;
        }

        // Restore from static storage if available, otherwise initialize to default
        if (si >= 0) {
            vartbl[vi].type = staticVars[si].type;
            vartbl[vi].val.ival = staticVars[si].ival;
            vartbl[vi].val.fval = staticVars[si].fval;
            if (staticVars[si].type == T_STR && vartbl[vi].val.sval) {
                strncpy(vartbl[vi].val.sval, staticVars[si].sval, STRINGSIZE - 1);
                vartbl[vi].val.sval[STRINGSIZE - 1] = '\0';
            }
            vartbl[vi].ndims = staticVars[si].ndims;
            memcpy(vartbl[vi].dims, staticVars[si].dims, sizeof(vartbl[vi].dims));
            vartbl[vi].arr = staticVars[si].arr;
        } else {
            // First call - initialize to default
            vartbl[vi].val.ival = 0;
            vartbl[vi].val.fval = 0.0;
            if (vartbl[vi].type == T_STR && vartbl[vi].val.sval) {
                vartbl[vi].val.sval[0] = '\0';
            }
            vartbl[vi].ndims = 0;
            vartbl[vi].arr = NULL;
        }
        varIsConst[vi] = false;

        // Skip comma
        while (*line == ' ') line++;
        if (*line == ',') { line++; continue; }
        break;
    }
}

// TRACE command - TROFF/TRON
void MMBasic_CmdTrace(void) {
    char *line = currentLine; while (*line == ' ') line++;
    char sub[16]; int i=0;
    while ((*line>='A'&&*line<='Z')||(*line>='a'&&*line<='z')&&i<15) sub[i++]=*line++; sub[i]=0;
    for (int j=0;sub[j];j++) if (sub[j]>='a'&&sub[j]<='z') sub[j]-=32;
    if (strcmp(sub,"ON")==0 || strcmp(sub,"TRON")==0) {
        traceOn = true; HAL_Display_Println("Trace ON");
    } else {
        traceOn = false; HAL_Display_Println("Trace OFF");
    }
}
// SUB/FUNCTION definition handler - skip to END SUB/END FUNCTION at runtime
void MMBasic_CmdSubFunDef(void) {
    int idx = -1;
    const char *line = lines[currentLineIndex];
    while (*line==' '||*line=='\t') line++;
    if ((line[0]=='S'||line[0]=='s')&&(line[1]=='U'||line[1]=='u')) line+=3;
    else line+=8;
    while (*line==' '||*line=='\t') line++;
    char name[MAXVARLEN+1]; int ni=0;
    while ((*line>='A'&&*line<='Z')||(*line>='a'&&*line<='z')||(*line>='0'&&*line<='9')||*line=='_'||*line=='$'||*line=='%'||*line=='!') {
        if (ni<MAXVARLEN) name[ni++]=*line; line++;
    } name[ni]=0;
    for (int j=0;name[j];j++) if (name[j]>='a'&&name[j]<='z') name[j]-=32;
    for (int k=0; k<subFunCount; k++) {
        if (strcmp(name, subFunTable[k].name)==0) { idx=k; break; }
    }
    if (idx>=0 && subFunTable[idx].endLine > currentLineIndex) {
        currentLineIndex = subFunTable[idx].endLine;
        flowControlActive = true;
    }
}
void MMBasic_CmdEndSubFun(void) {
    if (shadowBaseSP > 0) MMBasic_CmdReturn();
    // Otherwise just continue (definition skip)
}
// CALL command: CALL name(arg1, arg2, ...)
void MMBasic_CmdCall(void) {
    char *line = currentLine; while (*line==' ') line++;
    char name[MAXVARLEN+1]; int ni=0;
    while ((*line>='A'&&*line<='Z')||(*line>='a'&&*line<='z')||(*line>='0'&&*line<='9')||*line=='_'||*line=='$'||*line=='%'||*line=='!') {
        if (ni<MAXVARLEN) name[ni++]=*line; line++;
    } name[ni]=0;
    for (int j=0;name[j];j++) if (name[j]>='a'&&name[j]<='z') name[j]-=32;
    int idx=-1;
    for (int k=0; k<subFunCount; k++) {
        if (strcmp(name, subFunTable[k].name)==0 && !subFunTable[k].isFunction) {
            idx=k; break;
        }
    }
    if (idx<0) { MMBasic_Error(ERR_SYNTAX,"Unknown subroutine"); return; }
    
    // Save current SUB index and set new one
    if (subFunIdxSP < 16) subFunIdxStack[subFunIdxSP++] = currentSubFunIdx;
    currentSubFunIdx = idx;
    
    // Save return position
    subFunTable[idx].returnLine = currentLineIndex;
    subFunCallReturnIdx = currentLineIndex;
    if (shadowBaseSP < 16) subFunRetStack[shadowBaseSP] = subFunCallReturnIdx;
    
    // Push shadow base
    if (shadowBaseSP>=16) { MMBasic_Error(ERR_STACK_FULL,"SUB nesting too deep"); return; }
    shadowBase[shadowBaseSP++] = shadowPtr;
    
    // Parse arguments
    int args[8]; float fargs[8]; char *sargs[8]; int atypes[8];
    int argc=0;
    while (*line==' '||*line=='\t') line++;
    if (*line=='(') {
        line++;
        while (*line && *line!=')' && argc<8) {
            while (*line==' '||*line==','||*line=='\t') line++;
            if (*line==')') break;
            atypes[argc]=T_INT;
            if (MMBasic_EvaluateExpression(&line,&atypes[argc],&args[argc],&fargs[argc],&sargs[argc])) return;
            argc++;
        }
    }
    
    // Push shadows for parameters
    for (int p=0; p<subFunTable[idx].paramCount; p++) {
        char *pn = subFunTable[idx].paramNames[p];
        int vi = MMBasic_FindVariable(pn);
        // Save old value
        if (shadowPtr >= MAX_SHADOW) break;
        if (vi>=0) {
            shadowStack[shadowPtr].varIndex = vi;
            shadowStack[shadowPtr].type = vartbl[vi].type;
            shadowStack[shadowPtr].ival = vartbl[vi].val.ival;
            shadowStack[shadowPtr].fval = vartbl[vi].val.fval;
            if (vartbl[vi].type==T_STR && vartbl[vi].val.sval) {
                shadowStack[shadowPtr].sval = (char*)malloc(STRINGSIZE);
                strcpy(shadowStack[shadowPtr].sval, vartbl[vi].val.sval);
            } else shadowStack[shadowPtr].sval = NULL;
            shadowStack[shadowPtr].ndims = vartbl[vi].ndims;
            memcpy(shadowStack[shadowPtr].dims, vartbl[vi].dims, sizeof(vartbl[vi].dims));
            shadowStack[shadowPtr].arr = vartbl[vi].arr;
            shadowStack[shadowPtr].wasConst = varIsConst[vi];
            shadowStack[shadowPtr].isStatic = false;
        } else {
            shadowStack[shadowPtr].varIndex = -1;
            shadowStack[shadowPtr].isStatic = false;
            vi = MMBasic_CreateVariable(pn, (p<argc && atypes[p]==T_STR) ? T_STR : T_INT);
        }
        shadowPtr++;
        
        // Set new value
        if (vi>=0 && p<argc) {
            varIsConst[vi]=false;
            if (atypes[p]==T_STR) {
                if (!vartbl[vi].val.sval) vartbl[vi].val.sval=(char*)malloc(STRINGSIZE);
                strncpy(vartbl[vi].val.sval, sargs[p]?sargs[p]:"", STRINGSIZE-1);
                vartbl[vi].type=T_STR;
            } else {
                vartbl[vi].val.ival=args[p];
                vartbl[vi].type=atypes[p];
            }
        }
    }
    
    // Jump to SUB body (line after definition)
    currentLineIndex = subFunTable[idx].startLine + 1;
    currentLine = lines[currentLineIndex];
    flowControlActive = true;
}
// SORT array() [, direction] [, start [, count]]
// direction: 0 or omitted = ascending, non-zero = descending
void MMBasic_CmdSort(void) {
    char *line = currentLine; while (*line == ' ') line++;
    char name[MAXVARLEN+2]; int i=0;
    while ((*line>='A'&&*line<='Z')||(*line>='a'&&*line<='z')||(*line>='0'&&*line<='9')||*line=='_'||*line=='$'||*line=='!'||*line=='%') {
        if (i<MAXVARLEN+1) name[i++]=*line; line++;
    } name[i]=0;
    for (int j=0;name[j];j++) if (name[j]>='a'&&name[j]<='z') name[j]-=32;
    while (*line==' ') line++;
    if (*line!='(') { MMBasic_Error(ERR_SYNTAX,"Expected ("); return; } line++;
    while (*line==' ') line++;
    if (*line==')') line++;
    int idx = MMBasic_FindVariable(name);
    if (idx<0||vartbl[idx].ndims==0) { MMBasic_Error(ERR_TYPE,"Not an array"); return; }

    // Calculate total element count from dimensions
    int total = 1;
    for (int d = 0; d < vartbl[idx].ndims; d++) total *= (vartbl[idx].dims[d] + 1);

    // Parse optional parameters: [, direction] [, start [, count]]
    int direction = 0;
    int start = 0;
    int count = total;

    while (*line==' ') line++;
    if (*line==',') {
        line++; while (*line==' ') line++;
        int t; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &t, &direction, &f, &s)) return;
        while (*line==' ') line++;
        if (*line==',') {
            line++; while (*line==' ') line++;
            if (MMBasic_EvaluateExpression(&line, &t, &start, &f, &s)) return;
            while (*line==' ') line++;
            if (*line==',') {
                line++; while (*line==' ') line++;
                if (MMBasic_EvaluateExpression(&line, &t, &count, &f, &s)) return;
            }
        }
    }

    // Clamp start and count
    if (start < 0) start = 0;
    if (start >= total) return;
    if (count <= 0) return;
    if (start + count > total) count = total - start;
    if (count < 2) return;

    int descending = (direction != 0);

    if (vartbl[idx].type == T_INT) {
        int *a = vartbl[idx].arr;
        // Insertion sort on a[start..start+count-1]
        for (int i = start + 1; i < start + count; i++) {
            int key = a[i];
            int j = i - 1;
            if (descending) {
                while (j >= start && a[j] < key) { a[j+1] = a[j]; j--; }
            } else {
                while (j >= start && a[j] > key) { a[j+1] = a[j]; j--; }
            }
            a[j+1] = key;
        }
    } else if (vartbl[idx].type == T_FLOAT) {
        float *a = (float *)vartbl[idx].arr;
        for (int i = start + 1; i < start + count; i++) {
            float key = a[i];
            int j = i - 1;
            if (descending) {
                while (j >= start && a[j] < key) { a[j+1] = a[j]; j--; }
            } else {
                while (j >= start && a[j] > key) { a[j+1] = a[j]; j--; }
            }
            a[j+1] = key;
        }
    } else if (vartbl[idx].type == T_STR) {
        char **a = (char **)vartbl[idx].arr;
        for (int i = start + 1; i < start + count; i++) {
            char *key = a[i];
            int j = i - 1;
            if (descending) {
                while (j >= start && a[j] != NULL && key != NULL && strcmp(a[j], key) < 0) {
                    a[j+1] = a[j]; j--;
                }
            } else {
                while (j >= start && a[j] != NULL && key != NULL && strcmp(a[j], key) > 0) {
                    a[j+1] = a[j]; j--;
                }
            }
            a[j+1] = key;
        }
    } else {
        MMBasic_Error(ERR_TYPE, "Cannot sort this array type");
    }
}
// PRINT# and INPUT# already work with file numbers
static HardwareSerial *serDev = NULL;
void MMBasic_CmdOption(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    // For now, options are stubs - just parse and ignore
    // Common MMBasic options: BASE, EXPLICIT, ANGLE, etc.
    HAL_Display_Println("OPTION - stub");
}
void MMBasic_CmdLoop(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    
    int postWhile = 0, postUntil = 0;
    if ((line[0]=='W'||line[0]=='w') && (line[1]=='H'||line[1]=='h') &&
        (line[2]=='I'||line[2]=='i') && (line[3]=='L'||line[3]=='l') &&
        (line[4]=='E'||line[4]=='e')) {
        postWhile = 1; line += 5;
    } else if ((line[0]=='U'||line[0]=='u') && (line[1]=='N'||line[1]=='n') &&
               (line[2]=='T'||line[2]=='t') && (line[3]=='I'||line[3]=='i') &&
               (line[4]=='L'||line[4]=='l')) {
        postUntil = 1; line += 5;
    }
    
    if (dostackptr == 0) { MMBasic_Error(ERR_SYNTAX, "LOOP without DO"); return; }
    
    int keepLooping = 1;
    if (postWhile || postUntil) {
        int itype, ival; float fval; char *sval;
        if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
        if ((postWhile && !ival) || (postUntil && ival)) keepLooping = 0;
    }
    
    if (keepLooping) {
        currentLineIndex = dostack[dostackptr - 1];
        currentLine = lines[currentLineIndex];
        flowControlActive = true;
    } else {
        dostackptr--; // Exit loop
    }
}
void MMBasic_CmdRestore(void) {
    dataLineIdx = 0;
    dataOffset = 0;
}

// WHILE command - pre-test loop
void MMBasic_CmdWhile(void) {
    // Check if this is a re-entry (WEND jumped back) or first entry
    int isReentry = (whilestackptr > 0 && whilestack[whilestackptr - 1] == currentLineIndex);
    
    char *line = currentLine;
    while (*line == ' ') line++;
    
    int itype, ival;
    float fval; char *sval;
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
    
    if (ival) {
        // Condition true - push on first entry, continue on re-entry
        if (!isReentry) {
            if (whilestackptr >= MAXFORLOOPS) {
                MMBasic_Error(ERR_STACK_FULL, "WHILE stack full");
                return;
            }
            whilestack[whilestackptr++] = currentLineIndex;
        }
    } else {
        // Condition false - pop stack and skip to WEND
        if (isReentry) whilestackptr--;
        int depth = 1;
        currentLineIndex++;
        while (currentLineIndex < linecnt) {
            const char *l = lines[currentLineIndex];
            while (*l == ' ' || *l == '\t') l++;
            if ((l[0] == 'W' || l[0] == 'w') && (l[1] == 'H' || l[1] == 'h') &&
                (l[2] == 'I' || l[2] == 'i') && (l[3] == 'L' || l[3] == 'l') &&
                (l[4] == 'E' || l[4] == 'e')) depth++;
            else if ((l[0] == 'W' || l[0] == 'w') && (l[1] == 'E' || l[1] == 'e') &&
                     (l[2] == 'N' || l[2] == 'n') && (l[3] == 'D' || l[3] == 'd')) {
                depth--;
                if (depth == 0) { currentLineIndex++; break; }
            }
            currentLineIndex++;
        }
        flowControlActive = true;
    }
}

// SELECT CASE command
// SELECT CASE expression
void MMBasic_CmdSelect(void) {
    char *line = currentLine;
    // Skip "CASE" keyword if present: SELECT CASE expr
    while (*line == ' ') line++;
    if ((line[0]=='C'||line[0]=='c') && (line[1]=='A'||line[1]=='a') &&
        (line[2]=='S'||line[2]=='s') && (line[3]=='E'||line[3]=='e')) line += 4;
    
    int itype, ival; float fval; char *sval;
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
    
    if (selectptr >= 8) { MMBasic_Error(ERR_STACK_FULL, "SELECT stack full"); return; }
    selectstack[selectptr].matchType = itype;
    if (itype == T_STR) {
        strncpy(selectstack[selectptr].matchStr, sval ? sval : "", STRINGSIZE - 1);
        selectstack[selectptr].matchStr[STRINGSIZE - 1] = '\0';
        selectstack[selectptr].matchValue = 0;
    } else {
        selectstack[selectptr].matchValue = ival;
        selectstack[selectptr].matchStr[0] = '\0';
    }
    selectstack[selectptr].matched = false;
    selectptr++;
}

// CASE command
void MMBasic_CmdCase(void) {
    if (selectptr == 0) { MMBasic_Error(ERR_SYNTAX, "CASE without SELECT"); return; }
    s_select *sel = &selectstack[selectptr - 1];
    
    char *line = currentLine;
    while (*line == ' ') line++;
    
    // Check for CASE ELSE
    if ((line[0]=='E'||line[0]=='e') && (line[1]=='L'||line[1]=='l') &&
        (line[2]=='S'||line[2]=='s') && (line[3]=='E'||line[3]=='e')) {
        if (!sel->matched) {
            sel->matched = true;
        } else {
            MMBasic_SkipToEndSelect();
        }
        return;
    }
    
    // Parse comma-separated CASE values (supports plain values, IS comparisons, and TO ranges)
    bool match = false;
    while (*line != '\0' && *line != '\n' && *line != '\r') {
        while (*line == ' ') line++;
        
        // Check for IS comparison: IS > val, IS < val, etc.
        if ((line[0]=='I'||line[0]=='i') && (line[1]=='S'||line[1]=='s') && !isalpha((unsigned char)line[2])) {
            line += 2;
            while (*line == ' ') line++;
            // Parse comparison operator
            int op = 0; // 1=<>, 2=<, 3=>, 4===, 5=<=, 6=>=
            if (line[0]=='<' && line[1]=='>') { op = 1; line += 2; }
            else if (line[0]=='<' && line[1]=='=') { op = 5; line += 2; }
            else if (line[0]=='>' && line[1]=='=') { op = 6; line += 2; }
            else if (line[0]=='<') { op = 2; line++; }
            else if (line[0]=='>') { op = 3; line++; }
            else if (line[0]=='=') { op = 4; line++; }
            while (*line == ' ') line++;
            int t2, v2; float f2; char *s2;
            if (MMBasic_EvaluateExpression(&line, &t2, &v2, &f2, &s2)) return;
            if (sel->matchType == T_STR && t2 == T_STR) {
                const char *ms = sel->matchStr;
                const char *vs = s2 ? s2 : "";
                int cmp = strcmp(ms, vs);
                switch (op) {
                    case 1: if (cmp != 0) match = true; break;
                    case 2: if (cmp < 0)  match = true; break;
                    case 3: if (cmp > 0)  match = true; break;
                    case 4: if (cmp == 0) match = true; break;
                    case 5: if (cmp <= 0) match = true; break;
                    case 6: if (cmp >= 0) match = true; break;
                }
            } else {
                int mv = sel->matchValue;
                switch (op) {
                    case 1: if (mv != v2) match = true; break;
                    case 2: if (mv < v2)  match = true; break;
                    case 3: if (mv > v2)  match = true; break;
                    case 4: if (mv == v2) match = true; break;
                    case 5: if (mv <= v2) match = true; break;
                    case 6: if (mv >= v2) match = true; break;
                }
            }
        }
        // Plain value or TO range
        else {
            int t1, v1; float f1; char *s1;
            if (MMBasic_EvaluateExpression(&line, &t1, &v1, &f1, &s1)) return;
            while (*line == ' ') line++;
            // Check for TO keyword
            if ((line[0]=='T'||line[0]=='t') && (line[1]=='O'||line[1]=='o') && !isalpha((unsigned char)line[2])) {
                line += 2;
                while (*line == ' ') line++;
                int t2, v2; float f2; char *s2;
                if (MMBasic_EvaluateExpression(&line, &t2, &v2, &f2, &s2)) return;
                if (sel->matchType == T_STR && t1 == T_STR && t2 == T_STR) {
                    const char *ms = sel->matchStr;
                    const char *lo = s1 ? s1 : "";
                    const char *hi = s2 ? s2 : "";
                    if (strcmp(ms, lo) >= 0 && strcmp(ms, hi) <= 0) match = true;
                } else {
                    int mv = sel->matchValue;
                    if (mv >= v1 && mv <= v2) match = true;
                }
            } else {
                // Plain value comparison
                if (sel->matchType == T_STR && t1 == T_STR) {
                    if (strcmp(sel->matchStr, s1 ? s1 : "") == 0) match = true;
                } else {
                    if (v1 == sel->matchValue) match = true;
                }
            }
        }
        while (*line == ' ') line++;
        if (*line == ',') line++;
    }
    
    if (match && !sel->matched) {
        sel->matched = true;
    } else if (!match && sel->matched) {
        MMBasic_SkipToEndSelect();
    } else if (!match && !sel->matched) {
        MMBasic_SkipToNextCase();
    }
}

// Skip to next CASE or ENDSELECT at same nesting level
void MMBasic_SkipToNextCase(void) {
    int depth = 0;
    currentLineIndex++;
    while (currentLineIndex < linecnt) {
        const char *l = lines[currentLineIndex];
        while (*l == ' ' || *l == '\t') l++;
        if ((l[0]=='S'||l[0]=='s') && (l[1]=='E'||l[1]=='e') &&
            (l[2]=='L'||l[2]=='l') && (l[3]=='E'||l[3]=='e') &&
            (l[4]=='C'||l[4]=='c') && (l[5]=='T'||l[5]=='t')) {
            depth++;
        } else if (depth == 0 && (l[0]=='C'||l[0]=='c') && (l[1]=='A'||l[1]=='a') &&
                   (l[2]=='S'||l[2]=='s') && (l[3]=='E'||l[3]=='e')) {
            break; // Stop at next CASE, don't skip it
        } else if ((l[0]=='E'||l[0]=='e') && (l[1]=='N'||l[1]=='n') &&
                   (l[2]=='D'||l[2]=='d') && (l[3]=='S'||l[3]=='s') &&
                   (l[4]=='E'||l[4]=='e') && (l[5]=='L'||l[5]=='l') &&
                   (l[6]=='E'||l[6]=='e') && (l[7]=='C'||l[7]=='c') &&
                   (l[8]=='T'||l[8]=='t')) {
            if (depth == 0) { currentLineIndex++; break; }
            depth--;
        }
        currentLineIndex++;
    }
    flowControlActive = true;
}

// Skip to END SELECT at same nesting level
void MMBasic_SkipToEndSelect(void) {
    int depth = 0;
    currentLineIndex++;
    while (currentLineIndex < linecnt) {
        const char *l = lines[currentLineIndex];
        while (*l == ' ' || *l == '\t') l++;
        if ((l[0]=='S'||l[0]=='s') && (l[1]=='E'||l[1]=='e') &&
            (l[2]=='L'||l[2]=='l') && (l[3]=='E'||l[3]=='e') &&
            (l[4]=='C'||l[4]=='c') && (l[5]=='T'||l[5]=='t')) {
            depth++;
        } else if ((l[0]=='E'||l[0]=='e') && (l[1]=='N'||l[1]=='n') &&
                   (l[2]=='D'||l[2]=='d') && (l[3]=='S'||l[3]=='s') &&
                   (l[4]=='E'||l[4]=='e') && (l[5]=='L'||l[5]=='l') &&
                   (l[6]=='E'||l[6]=='e') && (l[7]=='C'||l[7]=='c') &&
                   (l[8]=='T'||l[8]=='t')) {
            if (depth == 0) { currentLineIndex++; break; }
            depth--;
        }
        currentLineIndex++;
    }
    flowControlActive = true;
}

// END SELECT command
void MMBasic_CmdEndSelect(void) {
    if (selectptr > 0) selectptr--;
}
void MMBasic_CmdWend(void) {
    if (whilestackptr == 0) {
        MMBasic_Error(ERR_SYNTAX, "WEND without WHILE");
        return;
    }
    currentLineIndex = whilestack[whilestackptr - 1];
    currentLine = lines[currentLineIndex];
    flowControlActive = true;
}

// Scan program for SUB/FUNCTION definitions
void ScanSubFunDefs(void) {
    subFunCount = 0;
    for (int i = 0; i < linecnt; i++) {
        const char *l = lines[i];
        while (*l == ' ' || *l == '\t') l++;
        int isSub = 0, isFun = 0;
        if ((l[0]=='S'||l[0]=='s')&&(l[1]=='U'||l[1]=='u')&&(l[2]=='B'||l[2]=='b')) isSub=1;
        else if ((l[0]=='F'||l[0]=='f')&&(l[1]=='U'||l[1]=='u')&&(l[2]=='N'||l[2]=='n')) isFun=1;
        if (!isSub && !isFun) continue;
        if (subFunCount >= MAXSUBFUN) break;
        l += (isSub ? 3 : 8); while (*l==' '||*l=='\t') l++;
        char *name = subFunTable[subFunCount].name; int ni=0;
        while ((*l>='A'&&*l<='Z')||(*l>='a'&&*l<='z')||(*l>='0'&&*l<='9')||*l=='_'||*l=='$'||*l=='%'||*l=='!') {
            if (ni<MAXVARLEN) name[ni++]=*l; l++;
        } name[ni]=0;
        for (int j=0;name[j];j++) if (name[j]>='a'&&name[j]<='z') name[j]-=32;
        if (name[0]==0) continue;
        subFunTable[subFunCount].startLine = i;
        subFunTable[subFunCount].isFunction = isFun;
        int pc=0;
        while (*l==' '||*l=='\t') l++;
        if (*l=='(') { l++;
            while (*l && *l!=')' && pc<8) {
                while (*l==' '||*l==','||*l=='\t') l++;
                char *pn = subFunTable[subFunCount].paramNames[pc]; int pni=0;
                while ((*l>='A'&&*l<='Z')||(*l>='a'&&*l<='z')||(*l>='0'&&*l<='9')||*l=='_'||*l=='$'||*l=='%'||*l=='!') {
                    if (pni<MAXVARLEN) pn[pni++]=*l; l++;
                } pn[pni]=0;
                for (int j=0;pn[j];j++) if (pn[j]>='a'&&pn[j]<='z') pn[j]-=32;
                if (pn[0]) pc++;
            }
            if (*l==')') l++;
        }
        subFunTable[subFunCount].paramCount = pc;
        int depth = 1, ei = i+1;
        while (ei < linecnt && depth > 0) {
            const char *el = lines[ei];
            while (*el==' '||*el=='\t') el++;
            if ((el[0]=='S'||el[0]=='s')&&(el[1]=='U'||el[1]=='u')&&(el[2]=='B'||el[2]=='b')) depth++;
            else if ((el[0]=='F'||el[0]=='f')&&(el[1]=='U'||el[1]=='u')&&(el[2]=='N'||el[2]=='n')) depth++;
            else if ((el[0]=='E'||el[0]=='e')&&(el[1]=='N'||el[1]=='n')&&(el[2]=='D'||el[2]=='d')) {
                el+=3; while (*el==' '||*el=='\t') el++;
                if ((el[0]=='S'||el[0]=='s')&&(el[1]=='U'||el[1]=='u')&&(el[2]=='B'||el[2]=='b')) depth--;
                else if ((el[0]=='F'||el[0]=='f')&&(el[1]=='U'||el[1]=='u')&&(el[2]=='N'||el[2]=='n')) depth--;
            }
            if (depth > 0) ei++;
        }
        subFunTable[subFunCount].endLine = (depth==0) ? ei : i;
        subFunCount++;
    }
}

// ============ TURTLE Graphics (ported from PicoMite) ============
#define T_HRES 240
#define T_VRES 134
#define T_MAX_POLY 128
#define T_MAX_CURSOR_BUF 50*50*2

struct TurtleState {
    float x, y, heading;
    int penDown, penColor, penWidth;
    int fillColor, fillEnabled, fillPattern;
    int visible, cursorSize, cursorColor;
    int cursorX, cursorY; float cursorHeading; int cursorDrawn;
    unsigned char *cursorBuf;
    int cursorBufX1, cursorBufY1, cursorBufX2, cursorBufY2;
    float stackX[16], stackY[16], stackH[16]; int stackPtr;
    int polyX[T_MAX_POLY], polyY[T_MAX_POLY], polyCount, polyRecording;
};
static TurtleState t;

// --- Drawing helpers adapted for M5Cardputer ---

// Thick line with perpendicular offset (PicoMite convention: negative width = centered)
static void tDrawLine(int x1, int y1, int x2, int y2, int width, uint16_t color) {
    if (width == 1) {
        M5Cardputer.Display.drawLine(x1, y1, x2, y2, color);
        return;
    }
    int w = abs(width);
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1);
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f) { M5Cardputer.Display.drawPixel(x1, y1, color); return; }
    float nx = -dy / len, ny = dx / len;
    int half = w / 2;
    for (int i = -half; i <= half; i++) {
        int ox = (int)(nx * i + 0.5f), oy = (int)(ny * i + 0.5f);
        M5Cardputer.Display.drawLine(x1 + ox, y1 + oy, x2 + ox, y2 + oy, color);
    }
}

static void tDrawPixel(int x, int y, uint16_t color) {
    if (x >= 0 && x < T_HRES && y >= 0 && y < T_VRES)
        M5Cardputer.Display.drawPixel(x, y, color);
}

static float tNormalizeHeading(float h) {
    while (h < 0) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;
    return h;
}

// --- Cursor with pixel save/restore ---
static void tEraseCursor(void) {
    if (!t.cursorDrawn) return;
    // Restore saved pixels
    if (t.cursorBuf) {
        int idx = 0;
        for (int y = t.cursorBufY1; y <= t.cursorBufY2; y++)
            for (int x = t.cursorBufX1; x <= t.cursorBufX2; x++) {
                uint16_t c = (t.cursorBuf[idx] << 8) | t.cursorBuf[idx + 1];
                if (x >= 0 && x < T_HRES && y >= 0 && y < T_VRES)
                    M5Cardputer.Display.drawPixel(x, y, c);
                idx += 2;
            }
        free(t.cursorBuf); t.cursorBuf = NULL;
    } else {
        // Fallback: draw black triangle
        float rad = t.cursorHeading * 3.14159f / 180.0f;
        int x1 = t.cursorX + (int)(t.cursorSize * sin(rad));
        int y1 = t.cursorY - (int)(t.cursorSize * cos(rad));
        float al = (t.cursorHeading + 150) * 3.14159f / 180.0f;
        int x2 = t.cursorX + (int)(t.cursorSize * 0.6f * sin(al));
        int y2 = t.cursorY - (int)(t.cursorSize * 0.6f * cos(al));
        float ar = (t.cursorHeading - 150) * 3.14159f / 180.0f;
        int x3 = t.cursorX + (int)(t.cursorSize * 0.6f * sin(ar));
        int y3 = t.cursorY - (int)(t.cursorSize * 0.6f * cos(ar));
        tDrawLine(x1, y1, x2, y2, 1, 0);
        tDrawLine(x2, y2, x3, y3, 1, 0);
        tDrawLine(x3, y3, x1, y1, 1, 0);
    }
    t.cursorDrawn = 0;
}

static void tDrawCursor(void) {
    if (!t.visible) return;
    int cx = (int)t.x, cy = (int)t.y;
    // Check if redraw is needed
    static int lastSize = 0, lastColor = 0;
    bool needsRedraw = !t.cursorDrawn ||
                       cx != t.cursorX || cy != t.cursorY ||
                       t.heading != t.cursorHeading ||
                       t.cursorSize != lastSize || t.cursorColor != lastColor;
    if (!needsRedraw) return;
    lastSize = t.cursorSize; lastColor = t.cursorColor;
    // Erase old cursor first
    if (t.cursorDrawn) tEraseCursor();
    float rad = t.heading * 3.14159f / 180.0f;
    int x1 = cx + (int)(t.cursorSize * sin(rad));
    int y1 = cy - (int)(t.cursorSize * cos(rad));
    float al = (t.heading + 150) * 3.14159f / 180.0f;
    int x2 = cx + (int)(t.cursorSize * 0.6f * sin(al));
    int y2 = cy - (int)(t.cursorSize * 0.6f * cos(al));
    float ar = (t.heading - 150) * 3.14159f / 180.0f;
    int x3 = cx + (int)(t.cursorSize * 0.6f * sin(ar));
    int y3 = cy - (int)(t.cursorSize * 0.6f * cos(ar));
    // Bounding box
    int bx1 = min(min(x1, x2), x3) - 2, by1 = min(min(y1, y2), y3) - 2;
    int bx2 = max(max(x1, x2), x3) + 2, by2 = max(max(y1, y2), y3) + 2;
    if (bx1 < 0) bx1 = 0; if (by1 < 0) by1 = 0;
    if (bx2 >= T_HRES) bx2 = T_HRES - 1; if (by2 >= T_VRES) by2 = T_VRES - 1;
    // Save background
    int bw = bx2 - bx1 + 1, bh = by2 - by1 + 1;
    t.cursorBuf = (unsigned char*)malloc(bw * bh * 2);
    if (t.cursorBuf) {
        int idx = 0;
        for (int y = by1; y <= by2; y++)
            for (int x = bx1; x <= bx2; x++) {
                uint16_t c = M5Cardputer.Display.readPixel(x, y);
                t.cursorBuf[idx++] = c >> 8; t.cursorBuf[idx++] = c & 0xFF;
            }
        t.cursorBufX1 = bx1; t.cursorBufY1 = by1;
        t.cursorBufX2 = bx2; t.cursorBufY2 = by2;
    }
    tDrawLine(x1, y1, x2, y2, 1, t.cursorColor);
    tDrawLine(x2, y2, x3, y3, 1, t.cursorColor);
    tDrawLine(x3, y3, x1, y1, 1, t.cursorColor);
    t.cursorX = cx; t.cursorY = cy; t.cursorHeading = t.heading;
    t.cursorDrawn = 1;
}

// --- Core movement (ported from PicoMite) ---
static void tForward(float dist) {
    if (fabs(dist) < 0.001f) return;
    float ox = t.x, oy = t.y;
    float rad = t.heading * 3.14159f / 180.0f;
    t.x += dist * sin(rad);
    t.y -= dist * cos(rad);
    if (t.penDown)
        tDrawLine((int)ox, (int)oy, (int)t.x, (int)t.y, -t.penWidth, t.penColor);
    if (t.polyRecording && t.polyCount < T_MAX_POLY - 1) {
        t.polyX[t.polyCount] = (int)t.x;
        t.polyY[t.polyCount] = (int)t.y;
        t.polyCount++;
    }
    if (t.visible) tDrawCursor();
}

static void tGoto(float nx, float ny) {
    float ox = t.x, oy = t.y;
    t.x = nx; t.y = ny;
    if (t.penDown)
        tDrawLine((int)ox, (int)oy, (int)nx, (int)ny, -t.penWidth, t.penColor);
    if (t.polyRecording && t.polyCount < T_MAX_POLY - 1) {
        t.polyX[t.polyCount] = (int)nx;
        t.polyY[t.polyCount] = (int)ny;
        t.polyCount++;
    }
    if (t.visible) tDrawCursor();
}

static void tArc(float radius, float angle) {
    int segments = (int)(fabs(angle) / 5.0f) + 1;
    if (segments < 4) segments = 4;
    float step = angle / segments;
    float dist = 2.0f * radius * sin((step * 3.14159f / 180.0f) / 2.0f);
    for (int i = 0; i < segments; i++) {
        tForward(dist);
        t.heading = tNormalizeHeading(t.heading + step);
    }
}

// --- Scanline polygon fill (from PicoMite) ---
static void tFillScanline(int *px, int *py, int count, uint16_t color) {
    int minY = py[0], maxY = py[0];
    for (int i = 1; i < count; i++) {
        if (py[i] < minY) minY = py[i];
        if (py[i] > maxY) maxY = py[i];
    }
    for (int sy = minY; sy <= maxY; sy++) {
        int inter[128]; int ic = 0;
        for (int i = 0; i < count; i++) {
            int next = (i + 1) % count;
            int y1 = py[i], y2 = py[next];
            if ((y1 <= sy && y2 > sy) || (y2 <= sy && y1 > sy)) {
                int ix = px[i] + (sy - y1) * (px[next] - px[i]) / (y2 - y1);
                if (ic < 128) inter[ic++] = ix;
            }
        }
        for (int i = 0; i < ic - 1; i++)
            for (int j = i + 1; j < ic; j++)
                if (inter[i] > inter[j]) { int tmp = inter[i]; inter[i] = inter[j]; inter[j] = tmp; }
        for (int i = 0; i < ic - 1; i += 2)
            tDrawLine(inter[i], sy, inter[i + 1], sy, 1, color);
    }
}

static void tDrawFilledPolygon(void) {
    if (t.polyCount < 3) return;
    // Close polygon
    if (t.polyX[0] != t.polyX[t.polyCount - 1] || t.polyY[0] != t.polyY[t.polyCount - 1]) {
        if (t.polyCount < T_MAX_POLY - 1) {
            t.polyX[t.polyCount] = t.polyX[0];
            t.polyY[t.polyCount] = t.polyY[0];
            t.polyCount++;
        }
    }
    tFillScanline(t.polyX, t.polyY, t.polyCount, t.fillColor);
    if (t.penDown) {
        for (int i = 0; i < t.polyCount - 1; i++)
            tDrawLine(t.polyX[i], t.polyY[i], t.polyX[i+1], t.polyY[i+1], -t.penWidth, t.penColor);
    }
}

static void tReset(bool show) {
    if (t.visible && t.cursorDrawn) { t.visible = 0; tEraseCursor(); }
    M5Cardputer.Display.fillScreen(0);
    t.x = T_HRES / 2; t.y = T_VRES / 2; t.heading = 0;
    t.penDown = 1; t.penColor = 0xFFFF; t.penWidth = 1;
    t.fillColor = 0xFFFF; t.fillEnabled = 0; t.fillPattern = 0;
    t.cursorSize = 10; t.cursorColor = 0x07E0;
    t.cursorX = 0; t.cursorY = 0; t.cursorHeading = 0; t.cursorDrawn = 0;
    if (t.cursorBuf) { free(t.cursorBuf); t.cursorBuf = NULL; }
    t.stackPtr = 0; t.polyCount = 0; t.polyRecording = 0;
    t.visible = show;
    if (t.visible) tDrawCursor();
}

// --- Keyword matcher ---
static bool tMatch(char **p, const char *word) {
    char *s = *p;
    while (*s == ' ') s++;
    int len = strlen(word);
    for (int i = 0; i < len; i++) {
        char c = s[i]; if (c >= 'a' && c <= 'z') c -= 32;
        if (c != word[i]) return false;
    }
    if (s[len] != ' ' && s[len] != '\0' && s[len] != ',' && s[len] != '\n' && s[len] != '\r') return false;
    *p = s + len;
    return true;
}

// --- Command dispatch ---
void MMBasic_CmdTurtle(void) {
    char *line = (char*)currentLine;
    while (*line == ' ') line++;
    if (tMatch(&line, "RESET") || tMatch(&line, "INIT")) {
        int show = 0;
        while (*line == ' ') line++;
        if (*line && *line != '\n' && *line != '\r') {
            int tt; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &show, &f, &s)) return;
        }
        tReset(show != 0);
    } else if (tMatch(&line, "FORWARD") || tMatch(&line, "FD")) {
        int tt, d; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &tt, &d, &f, &s)) return;
        tForward(f != 0 ? f : (float)d);
    } else if (tMatch(&line, "BACK") || tMatch(&line, "BK") || tMatch(&line, "BACKWARD")) {
        int tt, d; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &tt, &d, &f, &s)) return;
        tForward(f != 0 ? -f : -(float)d);
    } else if (tMatch(&line, "RIGHT") || tMatch(&line, "RT")) {
        int a = 90;
        while (*line == ' ') line++;
        if (*line && *line != '\n' && *line != '\r') {
            int tt; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &a, &f, &s)) return;
        }
        if (t.visible) tEraseCursor();
        t.heading = tNormalizeHeading(t.heading + a);
        if (t.visible) tDrawCursor();
    } else if (tMatch(&line, "LEFT") || tMatch(&line, "LT")) {
        int a = 90;
        while (*line == ' ') line++;
        if (*line && *line != '\n' && *line != '\r') {
            int tt; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &a, &f, &s)) return;
        }
        if (t.visible) tEraseCursor();
        t.heading = tNormalizeHeading(t.heading - a);
        if (t.visible) tDrawCursor();
    } else if (tMatch(&line, "PEN")) {
        if (tMatch(&line, "UP") || tMatch(&line, "PU")) {
            t.penDown = 0;
        } else if (tMatch(&line, "DOWN") || tMatch(&line, "PD")) {
            t.penDown = 1;
        } else if (tMatch(&line, "COLOUR") || tMatch(&line, "COLOR") || tMatch(&line, "PC")) {
            int tt, c; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &c, &f, &s)) return;
            t.penColor = (uint16_t)c;
        } else if (tMatch(&line, "WIDTH") || tMatch(&line, "PW")) {
            int tt, w; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &w, &f, &s)) return;
            t.penWidth = (w < 1) ? 1 : (w > 50) ? 50 : w;
        }
    } else if (tMatch(&line, "HOME")) {
        tGoto(T_HRES / 2, T_VRES / 2);
        if (t.visible) { tEraseCursor(); t.heading = 0; tDrawCursor(); }
    } else if (tMatch(&line, "SETXY") || tMatch(&line, "MOVE")) {
        int tt; float f; char *s; int nx, ny;
        if (MMBasic_EvaluateExpression(&line, &tt, &nx, &f, &s)) return;
        while (*line == ' ' || *line == ',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &ny, &f, &s)) return;
        tGoto((float)nx, (float)ny);
    } else if (tMatch(&line, "SETX")) {
        int tt, nx; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &tt, &nx, &f, &s)) return;
        tGoto((float)nx, t.y);
    } else if (tMatch(&line, "SETY")) {
        int tt, ny; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &tt, &ny, &f, &s)) return;
        tGoto(t.x, (float)ny);
    } else if (tMatch(&line, "SETHEADING") || tMatch(&line, "SETH")) {
        int tt; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &tt, &tt, &f, &s)) return;
        if (t.visible) tEraseCursor();
        t.heading = tNormalizeHeading((float)tt);
        if (t.visible) tDrawCursor();
    } else if (tMatch(&line, "SET")) {
        if (tMatch(&line, "XY")) {
            int tt; float f; char *s; int nx, ny;
            if (MMBasic_EvaluateExpression(&line, &tt, &nx, &f, &s)) return;
            while (*line == ' ' || *line == ',') line++;
            if (MMBasic_EvaluateExpression(&line, &tt, &ny, &f, &s)) return;
            tGoto((float)nx, (float)ny);
        } else if (tMatch(&line, "X")) {
            int tt, nx; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &nx, &f, &s)) return;
            tGoto((float)nx, t.y);
        } else if (tMatch(&line, "Y")) {
            int tt, ny; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &ny, &f, &s)) return;
            tGoto(t.x, (float)ny);
        } else if (tMatch(&line, "HEADING")) {
            int tt; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &tt, &f, &s)) return;
            if (t.visible) tEraseCursor();
            t.heading = tNormalizeHeading((float)tt);
            if (t.visible) tDrawCursor();
        }
    } else if (tMatch(&line, "ARC")) {
        int tt; float f; char *s; int r, a;
        if (MMBasic_EvaluateExpression(&line, &tt, &r, &f, &s)) return;
        while (*line == ' ' || *line == ',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &a, &f, &s)) return;
        tArc((float)r, (float)a);
    } else if (tMatch(&line, "ARCL") || tMatch(&line, "ARCLEFT") || tMatch(&line, "ARC LEFT")) {
        int tt; float f; char *s; int r, a;
        if (MMBasic_EvaluateExpression(&line, &tt, &r, &f, &s)) return;
        while (*line == ' ' || *line == ',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &a, &f, &s)) return;
        tArc((float)r, (float)a);
    } else if (tMatch(&line, "ARCR") || tMatch(&line, "ARCRIGHT") || tMatch(&line, "ARC RIGHT")) {
        int tt; float f; char *s; int r, a;
        if (MMBasic_EvaluateExpression(&line, &tt, &r, &f, &s)) return;
        while (*line == ' ' || *line == ',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &a, &f, &s)) return;
        tArc((float)r, -(float)a);
    } else if (tMatch(&line, "BEZIER")) {
        int tt; float f; char *s; int cp1, a1, cp2, a2, ep, ea;
        if (MMBasic_EvaluateExpression(&line, &tt, &cp1, &f, &s)) return;
        while (*line==' '||*line==',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &a1, &f, &s)) return;
        while (*line==' '||*line==',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &cp2, &f, &s)) return;
        while (*line==' '||*line==',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &a2, &f, &s)) return;
        while (*line==' '||*line==',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &ep, &f, &s)) return;
        while (*line==' '||*line==',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &ea, &f, &s)) return;
        float h = t.heading, r1 = h+a1, r2 = h+a1+a2, re = h+a1+a2+ea;
        float p1x = t.x + cp1*sin(r1*3.14159f/180), p1y = t.y - cp1*cos(r1*3.14159f/180);
        float p2x = p1x + cp2*sin(r2*3.14159f/180), p2y = p1y - cp2*cos(r2*3.14159f/180);
        float ex = p2x + ep*sin(re*3.14159f/180), ey = p2y - ep*cos(re*3.14159f/180);
        float ox = t.x, oy = t.y;
        for (int seg = 1; seg <= 20; seg++) {
            float tt2 = seg/20.0f, u = 1.0f-tt2;
            float cx = u*u*u*ox+3*u*u*tt2*p1x+3*u*tt2*tt2*p2x+tt2*tt2*tt2*ex;
            float cy = u*u*u*oy+3*u*u*tt2*p1y+3*u*tt2*tt2*p2y+tt2*tt2*tt2*ey;
            if (t.penDown) tDrawLine((int)t.x, (int)t.y, (int)cx, (int)cy, -t.penWidth, t.penColor);
            t.x = cx; t.y = cy;
        }
        t.heading = tNormalizeHeading(re);
        if (t.visible) tDrawCursor();
    } else if (tMatch(&line, "CIRCLE")) {
        int tt, r; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &tt, &r, &f, &s)) return;
        M5Cardputer.Display.drawCircle((int)t.x, (int)t.y, r, t.penColor);
    } else if (tMatch(&line, "DOT")) {
        int sz = 5;
        while (*line == ' ') line++;
        if (*line && *line != '\n' && *line != '\r') {
            int tt; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &sz, &f, &s)) return;
        }
        M5Cardputer.Display.fillCircle((int)t.x, (int)t.y, sz, t.penColor);
    } else if (tMatch(&line, "FCIRCLE")) {
        int tt, r; float f; char *s;
        if (MMBasic_EvaluateExpression(&line, &tt, &r, &f, &s)) return;
        if (t.fillEnabled) M5Cardputer.Display.fillCircle((int)t.x, (int)t.y, r, t.fillColor);
        if (t.penDown) M5Cardputer.Display.drawCircle((int)t.x, (int)t.y, r, t.penColor);
    } else if (tMatch(&line, "FRECT") || tMatch(&line, "FRECTANGLE")) {
        int tt; float f; char *s; int w, h;
        if (MMBasic_EvaluateExpression(&line, &tt, &w, &f, &s)) return;
        while (*line == ' ' || *line == ',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &h, &f, &s)) return;
        int x = (int)t.x - w/2, y = (int)t.y - h/2;
        if (t.fillEnabled) M5Cardputer.Display.fillRect(x, y, w, h, t.fillColor);
        if (t.penDown) {
            tDrawLine(x, y, x+w-1, y, 1, t.penColor);
            tDrawLine(x, y+h-1, x+w-1, y+h-1, 1, t.penColor);
            tDrawLine(x, y, x, y+h-1, 1, t.penColor);
            tDrawLine(x+w-1, y, x+w-1, y+h-1, 1, t.penColor);
        }
    } else if (tMatch(&line, "ARECT") || tMatch(&line, "ARECTANGLE")) {
        int tt; float f; char *s; int w, h;
        if (MMBasic_EvaluateExpression(&line, &tt, &w, &f, &s)) return;
        while (*line == ' ' || *line == ',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &h, &f, &s)) return;
        float rad = t.heading * 3.14159f / 180.0f;
        float rp = (t.heading + 90) * 3.14159f / 180.0f;
        float dxw = (w/2)*sin(rad), dyw = (w/2)*cos(rad);
        float dxh = (h/2)*sin(rp), dyh = (h/2)*cos(rp);
        int cx = (int)t.x, cy = (int)t.y;
        int px[4], py[4];
        px[0] = cx-(int)(dxw+dxh); py[0] = cy+(int)(dyw+dyh);
        px[1] = cx+(int)(dxw-dxh); py[1] = cy-(int)(dyw+dyh);
        px[2] = cx+(int)(dxw+dxh); py[2] = cy-(int)(dyw-dyh);
        px[3] = cx-(int)(dxw-dxh); py[3] = cy+(int)(dyw-dyh);
        if (t.fillEnabled) {
            tFillScanline(px, py, 4, t.fillColor);
        }
        if (t.penDown) {
            for (int i = 0; i < 4; i++)
                tDrawLine(px[i], py[i], px[(i+1)%4], py[(i+1)%4], -t.penWidth, t.penColor);
        }
    } else if (tMatch(&line, "WEDGE")) {
        int tt; float f; char *s; int r, sa, ea;
        if (MMBasic_EvaluateExpression(&line, &tt, &r, &f, &s)) return;
        while (*line==' '||*line==',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &sa, &f, &s)) return;
        while (*line==' '||*line==',') line++;
        if (MMBasic_EvaluateExpression(&line, &tt, &ea, &f, &s)) return;
        int cx = (int)t.x, cy = (int)t.y;
        int segs = (int)(fabs((float)(ea-sa)) / 5.0f) + 1;
        int poly_x[128], poly_y[128]; int pc = 0;
        poly_x[pc] = cx; poly_y[pc] = cy; pc++;
        for (int i = 0; i <= segs && pc < 127; i++) {
            float a = (t.heading + sa + (float)(ea-sa)*i/segs) * 3.14159f/180;
            poly_x[pc] = cx + (int)(r * cos(a));
            poly_y[pc] = cy - (int)(r * sin(a));
            pc++;
        }
        if (t.fillEnabled) tFillScanline(poly_x, poly_y, pc, t.fillColor);
        if (t.penDown) {
            for (int i = 1; i < pc-1; i++)
                tDrawLine(poly_x[i], poly_y[i], poly_x[i+1], poly_y[i+1], -t.penWidth, t.penColor);
            tDrawLine(cx, cy, poly_x[1], poly_y[1], -t.penWidth, t.penColor);
            tDrawLine(cx, cy, poly_x[pc-1], poly_y[pc-1], -t.penWidth, t.penColor);
        }
    } else if (tMatch(&line, "FILL")) {
        if (tMatch(&line, "COLOUR") || tMatch(&line, "COLOR") || tMatch(&line, "FC")) {
            int tt, c; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &c, &f, &s)) return;
            t.fillColor = (uint16_t)c; t.fillEnabled = 1;
        } else if (tMatch(&line, "PATTERN") || tMatch(&line, "FP")) {
            int tt, p; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &p, &f, &s)) return;
            t.fillPattern = (p < 0) ? 0 : (p > 31) ? 31 : p;
        }
    } else if (tMatch(&line, "NO") && tMatch(&line, "FILL")) {
        t.fillEnabled = 0;
    } else if ((tMatch(&line, "BEGIN") && tMatch(&line, "FILL")) || tMatch(&line, "BF")) {
        t.polyCount = 0; t.polyRecording = 1;
        t.polyX[0] = (int)t.x; t.polyY[0] = (int)t.y; t.polyCount = 1;
    } else if ((tMatch(&line, "END") && tMatch(&line, "FILL")) || tMatch(&line, "EF")) {
        t.polyRecording = 0;
        if (t.polyCount > 2) {
            if (t.visible) tEraseCursor();
            tDrawFilledPolygon();
            if (t.visible) tDrawCursor();
        }
        t.polyCount = 0;
    } else if (tMatch(&line, "SHOW") || tMatch(&line, "ST") || tMatch(&line, "SHOWTURTLE")) {
        t.visible = 1; tDrawCursor();
    } else if (tMatch(&line, "HIDE") || tMatch(&line, "HT") || tMatch(&line, "HIDETURTLE")) {
        tEraseCursor(); t.visible = 0;
    } else if (tMatch(&line, "CURSOR")) {
        if (tMatch(&line, "SIZE") || tMatch(&line, "CS")) {
            int tt, sz; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &sz, &f, &s)) return;
            if (sz < 5) sz = 5; if (sz > 50) sz = 50;
            if (t.visible) tEraseCursor();
            t.cursorSize = sz;
            if (t.visible) tDrawCursor();
        } else if (tMatch(&line, "COLOUR") || tMatch(&line, "COLOR") || tMatch(&line, "CC")) {
            int tt, c; float f; char *s;
            if (MMBasic_EvaluateExpression(&line, &tt, &c, &f, &s)) return;
            if (t.visible) tEraseCursor();
            t.cursorColor = (uint16_t)c;
            if (t.visible) tDrawCursor();
        }
    } else if (tMatch(&line, "STAMP")) {
        // Simplified stamp
        int cx = (int)t.x, cy = (int)t.y;
        M5Cardputer.Display.fillCircle(cx, cy-3, 5, 0x07E0);
        M5Cardputer.Display.fillCircle(cx, cy+2, 7, 0x07E0);
        M5Cardputer.Display.fillRect(cx-3, cy-8, 6, 4, 0x07E0);
        M5Cardputer.Display.fillRect(cx-6, cy-1, 4, 3, 0x07E0);
        M5Cardputer.Display.fillRect(cx+2, cy-1, 4, 3, 0x07E0);
        M5Cardputer.Display.fillRect(cx-5, cy+6, 3, 3, 0x07E0);
        M5Cardputer.Display.fillRect(cx+2, cy+6, 3, 3, 0x07E0);
    } else if (tMatch(&line, "PUSH")) {
        if (t.stackPtr < 16) {
            t.stackX[t.stackPtr] = t.x; t.stackY[t.stackPtr] = t.y;
            t.stackH[t.stackPtr] = t.heading; t.stackPtr++;
        }
    } else if (tMatch(&line, "POP")) {
        if (t.stackPtr > 0) {
            t.stackPtr--;
            tGoto(t.stackX[t.stackPtr], t.stackY[t.stackPtr]);
            if (t.visible) { tEraseCursor(); t.heading = t.stackH[t.stackPtr]; tDrawCursor(); }
            else t.heading = t.stackH[t.stackPtr];
        }
    }
}
// ============ SPRITE System (pixel-based, PicoMite-compatible) ============
#define MAX_SPRITES 16
#define SPR_MAX_W 32
// RGB121 4-bit → 16-bit RGB565 conversion table
static const uint16_t pal16[16] = {
    0x0000, 0x001F, 0x0200, 0x021F, 0x0400, 0x041F, 0x07E0, 0x07FF,
    0xF800, 0xF81F, 0xF900, 0xF91F, 0xFC00, 0xFC1F, 0xFFE0, 0xFFFF
};
// Character '0'-'9','A'-'F' to palette index (space=transparent, -1)
static int sprCharToIdx(char c) {
    if (c==' ') return -1;
    if (c>='0'&&c<='9') return c-'0';
    if (c>='A'&&c<='F') return c-'A'+10;
    if (c>='a'&&c<='f') return c-'a'+10;
    return -1;
}
struct SpritePixel {
    uint8_t *pixels;    // 4-bit packed: 2 pixels/byte, low nibble = even col
    int w, h, x, y, nx, ny, layer;
    bool active;
};
static SpritePixel spr[MAX_SPRITES];
static uint16_t spr_transparent = 0; // Palette index 0 is transparent

static void sprDraw(int n) {
    if (!spr[n].active || !spr[n].pixels) return;
    uint8_t *p = spr[n].pixels;
    int w = spr[n].w, h = spr[n].h, x0 = spr[n].x, y0 = spr[n].y;
    for (int row=0; row<h; row++) {
        for (int col=0; col<w; col++) {
            int byteIdx = (row*w + col) >> 1;
            int nib = (col & 1) ? (p[byteIdx] >> 4) : (p[byteIdx] & 0xF);
            if (nib == spr_transparent) continue;
            M5Cardputer.Display.drawPixel(x0+col, y0+row, pal16[nib]);
        }
    }
}
static void sprErase(int n) {
    if (!spr[n].active) return;
    M5Cardputer.Display.fillRect(spr[n].x, spr[n].y, spr[n].w, spr[n].h, 0);
}

void MMBasic_CmdSprite(void) {
    char *line = (char*)currentLine;
    while (*line == ' ') line++;
    if (tMatch(&line, "SHOW")) {
        int tt, n, x=0, y=0, layer=1; float f; char *s;
        while (*line==' ') line++; if (*line=='#') line++;
        if (MMBasic_EvaluateExpression(&line,&tt,&n,&f,&s)) return;
        while (*line==' '||*line==',') line++; if (MMBasic_EvaluateExpression(&line,&tt,&x,&f,&s)) return;
        while (*line==' '||*line==',') line++; if (MMBasic_EvaluateExpression(&line,&tt,&y,&f,&s)) return;
        while (*line==' '||*line==',') line++; if (MMBasic_EvaluateExpression(&line,&tt,&layer,&f,&s)) ;
        if (n<0||n>=MAX_SPRITES||!spr[n].pixels) return;
        spr[n].active=1; spr[n].x=x; spr[n].y=y; spr[n].layer=layer;
        sprDraw(n);
    } else if (tMatch(&line, "HIDE")) {
        while (*line==' ') line++; if (*line=='#') line++;
        int tt, n; float f; char *s;
        if (MMBasic_EvaluateExpression(&line,&tt,&n,&f,&s)) return;
        if (n<0||n>=MAX_SPRITES) return;
        sprErase(n); spr[n].active=0;
    } else if (tMatch(&line, "NEXT")) {
        while (*line==' ') line++; if (*line=='#') line++;
        int tt, n, nx, ny; float f; char *s;
        if (MMBasic_EvaluateExpression(&line,&tt,&n,&f,&s)) return;
        while (*line==' '||*line==',') line++; if (MMBasic_EvaluateExpression(&line,&tt,&nx,&f,&s)) return;
        while (*line==' '||*line==',') line++; if (MMBasic_EvaluateExpression(&line,&tt,&ny,&f,&s)) return;
        if (n<0||n>=MAX_SPRITES) return;
        spr[n].nx=nx; spr[n].ny=ny;
    } else if (tMatch(&line, "MOVE")) {
        for (int i=0; i<MAX_SPRITES; i++) if (spr[i].active) sprErase(i);
        for (int i=0; i<MAX_SPRITES; i++) if (spr[i].active) {
            spr[i].x=spr[i].nx; spr[i].y=spr[i].ny;
            sprDraw(i);
        }
    } else if (tMatch(&line, "LOAD")) {
        int tt, n=1; float f; char *s;
        char fname[64]; int fi=0;
        while (*line==' ') line++;
        if (*line=='"') { line++; while (*line!='"'&&*line&&fi<63) fname[fi++]=*line++; fname[fi]=0; if (*line=='"') line++; }
        else { while (*line!=' '&&*line&&fi<63) fname[fi++]=*line++; fname[fi]=0; }
        while (*line==' '||*line==',') line++;
        if (MMBasic_EvaluateExpression(&line,&tt,&n,&f,&s)) ;
        if (!HAL_SD_Init()) return;
        char path[68]; path[0]='/'; strcpy(path+1,fname);
        File file = SD.open(path, FILE_READ);
        if (!file) return;
        // Read header: w, count, h (comma separated)
        String hdr = file.readStringUntil('\n'); hdr.trim();
        while (hdr.length()>0 && hdr[0]=='\'') { hdr = file.readStringUntil('\n'); hdr.trim(); }
        int ws=0, cnt=0, hs=0, ci=0;
        char *hp = (char*)hdr.c_str();
        while (*hp==' ') hp++; ws=atoi(hp); while (*hp&&*hp!=',') hp++; if (*hp==',') hp++;
        while (*hp==' ') hp++; cnt=atoi(hp); while (*hp&&*hp!=',') hp++; if (*hp==',') hp++;
        while (*hp==' ') hp++; hs=atoi(hp); if (hs==0) hs=ws;
        if (ws<=0||ws>SPR_MAX_W||hs<=0||hs>SPR_MAX_W) { file.close(); return; }
        int bufSize = (ws*hs+1)>>1;
        for (int idx=n; idx<n+cnt && idx<MAX_SPRITES; idx++) {
            spr[idx].w=ws; spr[idx].h=hs; spr[idx].active=0;
            if (spr[idx].pixels) free(spr[idx].pixels);
            spr[idx].pixels = (uint8_t*)malloc(bufSize);
            if (!spr[idx].pixels) { file.close(); return; }
            memset(spr[idx].pixels,0,bufSize);
            for (int row=0; row<hs && file.available(); row++) {
                String line = file.readStringUntil('\n'); line.trim();
                while (line.length()>0 && line[0]=='\'') { line = file.readStringUntil('\n'); line.trim(); }
                for (int col=0; col<ws && col<line.length(); col++) {
                    int ci = sprCharToIdx(line[col]);
                    if (ci<0) ci=0;
                    int byteIdx = (row*ws+col)>>1;
                    if (col&1) spr[idx].pixels[byteIdx] |= (ci<<4);
                    else spr[idx].pixels[byteIdx] = ci;
                }
            }
        }
        file.close();
    } else if (tMatch(&line, "CLOSE")) {
        if (tMatch(&line, "ALL")) {
            for (int i=0; i<MAX_SPRITES; i++) {
                if (spr[i].active) sprErase(i);
                spr[i].active=0;
                if (spr[i].pixels) { free(spr[i].pixels); spr[i].pixels=0; }
            }
        } else {
            while (*line==' ') line++; if (*line=='#') line++;
            int tt, n; float f; char *s;
            if (MMBasic_EvaluateExpression(&line,&tt,&n,&f,&s)) return;
            if (n>=0&&n<MAX_SPRITES) {
                if (spr[n].active) sprErase(n);
                spr[n].active=0;
                if (spr[n].pixels) { free(spr[n].pixels); spr[n].pixels=0; }
            }
        }
    } else if (tMatch(&line, "COPY")) {
        int tt, src, dst, cnt=1; float f; char *s;
        while (*line==' ') line++; if (*line=='#') line++;
        if (MMBasic_EvaluateExpression(&line,&tt,&src,&f,&s)) return;
        while (*line==' '||*line==',') line++; if (*line=='#') line++;
        if (MMBasic_EvaluateExpression(&line,&tt,&dst,&f,&s)) return;
        while (*line==' '||*line==',') line++; if (MMBasic_EvaluateExpression(&line,&tt,&cnt,&f,&s)) ;
        if (src<0||src>=MAX_SPRITES||!spr[src].pixels) return;
        for (int i=0; i<cnt && dst+i<MAX_SPRITES; i++) {
            int sz = (spr[src].w*spr[src].h+1)>>1;
            if (spr[dst+i].pixels) free(spr[dst+i].pixels);
            spr[dst+i].pixels = (uint8_t*)malloc(sz);
            memcpy(spr[dst+i].pixels, spr[src].pixels, sz);
            spr[dst+i].w=spr[src].w; spr[dst+i].h=spr[src].h;
            spr[dst+i].x=spr[src].x; spr[dst+i].y=spr[src].y;
            spr[dst+i].active=0;
        }
    }
}
// VAR SAVE/RESTORE to SD card
void MMBasic_CmdVar(void) {
    char *line = (char*)currentLine; while (*line==' ') line++;
    if (tMatch(&line, "SAVE")) {
        char fname[64]; int fi=0;
        while (*line==' ') line++;
        if (*line=='"') { line++; while (*line!='"'&&*line&&fi<63) fname[fi++]=*line++; fname[fi]=0; if (*line=='"') line++; }
        else { while (*line!=' '&&*line&&fi<63) fname[fi++]=*line++; fname[fi]=0; }
        if (!HAL_SD_Init()) return;
        char path[68]; path[0]='/'; strcpy(path+1,fname);
        File f = SD.open(path, FILE_WRITE);
        if (!f) return;
        for (int i=0; i<varcnt; i++) {
            f.print(vartbl[i].name); f.print(",");
            f.print((int)vartbl[i].type); f.print(",");
            if (vartbl[i].type==T_STR) {
                if (vartbl[i].val.sval) f.print(vartbl[i].val.sval); else f.print("");
            } else if (vartbl[i].type==T_FLOAT) {
                f.print(vartbl[i].val.fval, 6);
            } else {
                f.print(vartbl[i].val.ival);
            }
            f.println("");
        }
        f.close();
    } else if (tMatch(&line, "RESTORE")) {
        char fname[64]; int fi=0;
        while (*line==' ') line++;
        if (*line=='"') { line++; while (*line!='"'&&*line&&fi<63) fname[fi++]=*line++; fname[fi]=0; if (*line=='"') line++; }
        else { while (*line!=' '&&*line&&fi<63) fname[fi++]=*line++; fname[fi]=0; }
        if (!HAL_SD_Init()) return;
        char path[68]; path[0]='/'; strcpy(path+1,fname);
        File f = SD.open(path, FILE_READ);
        if (!f) return;
        varcnt=0; memset(vartbl,0,MAXVARS*sizeof(s_vartbl));
        while (f.available()) {
            String ln = f.readStringUntil('\n'); ln.trim();
            int c1=ln.indexOf(','), c2=ln.indexOf(',',c1+1);
            if (c1<0||c2<0) continue;
            String nm = ln.substring(0,c1);
            int tp = ln.substring(c1+1,c2).toInt();
            String vl = ln.substring(c2+1);
            char nbuf[33]; nm.toCharArray(nbuf,33);
            int idx = MMBasic_CreateVariable(nbuf, tp);
            if (idx>=0) {
                if (tp==T_STR) {
                    if (!vartbl[idx].val.sval) vartbl[idx].val.sval=(char*)malloc(STRINGSIZE);
                    vl.toCharArray(vartbl[idx].val.sval, STRINGSIZE);
                } else if (tp==T_FLOAT) vartbl[idx].val.fval=vl.toFloat();
                else vartbl[idx].val.ival=vl.toInt();
                vartbl[idx].type=tp;
            }
        }
        f.close();
    } else if (tMatch(&line, "CLEAR")) {
        varcnt=0; memset(vartbl,0,MAXVARS*sizeof(s_vartbl));
    }
}
// RGB(r, g, b) - create 16-bit color from RGB values
uint16_t MMBasic_RGB(int r, int g, int b) {
    // Convert 8-bit RGB to 16-bit 565 format
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}





// CHAIN - Load and run another program
void MMBasic_CmdChain(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    char filename[FILENAME_LENGTH];
    int i = 0;
    if (*line == '"') {
        line++;
        while (*line != '"' && *line != '\0' && i < FILENAME_LENGTH - 1) filename[i++] = *line++;
        if (*line == '"') line++;
    } else {
        while (*line != ' ' && *line != '\0' && *line != '\n' && i < FILENAME_LENGTH - 1) filename[i++] = *line++;
    }
    filename[i] = '\0';
    if (i == 0) { MMBasic_Error(ERR_SYNTAX, "Filename required"); return; }
    MMBasic_LoadProgram(filename);
    MMBasic_RunProgram();
}

// MERGE - Merge program lines from file
void MMBasic_CmdMerge(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    char filename[FILENAME_LENGTH];
    int i = 0;
    if (*line == '"') {
        line++;
        while (*line != '"' && *line != '\0' && i < FILENAME_LENGTH - 1) filename[i++] = *line++;
        if (*line == '"') line++;
    } else {
        while (*line != ' ' && *line != '\0' && *line != '\n' && i < FILENAME_LENGTH - 1) filename[i++] = *line++;
    }
    filename[i] = '\0';
    if (i == 0) { MMBasic_Error(ERR_SYNTAX, "Filename required"); return; }
    if (!HAL_SD_Init()) { MMBasic_Error(ERR_FILE_IO, "SD card not ready"); return; }
    char path[FILENAME_LENGTH + 2];
    path[0] = '/';
    strcpy(path + 1, filename);
    File file = SD.open(path, FILE_READ);
    if (!file) { MMBasic_Error(ERR_FILE_IO, "File not found"); return; }
    while (file.available()) {
        String ln = file.readStringUntil('\n');
        ln.trim();
        if (ln.length() == 0) continue;
        char buf[STRINGSIZE];
        ln.toCharArray(buf, STRINGSIZE);
        if (buf[0] >= '0' && buf[0] <= '9') {
            int linenum = atoi(buf);
            char *rest = buf;
            while (*rest >= '0' && *rest <= '9') rest++;
            while (*rest == ' ') rest++;
            MMBasic_StoreLine(linenum, rest);
        }
    }
    file.close();
}

// EXECUTE - Execute string as BASIC code
void MMBasic_CmdExecute(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    int itype, ival;
    float fval;
    char *sval;
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
    if (itype == T_STR && sval != NULL) {
        char execBuf[STRINGSIZE];
        strncpy(execBuf, sval, STRINGSIZE - 1);
        execBuf[STRINGSIZE - 1] = '\0';
        MMBasic_Execute(execBuf);
    }
}

// DELETE - Delete program line(s)
void MMBasic_CmdDelete(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    int itype, ival;
    float fval;
    char *sval;
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
    int startLine = ival;
    int endLine = startLine;
    while (*line == ' ') line++;
    if (*line == '-') {
        line++;
        while (*line == ' ') line++;
        if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
        endLine = ival;
    }
    for (int j = startLine; j <= endLine; j++) {
        MMBasic_StoreLine(j, NULL);
    }
}

// FLUSH - Flush file buffers
void MMBasic_CmdFlush(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    if (*line == '#') line++;
    while (*line == ' ') line++;
    int itype, ival;
    float fval;
    char *sval;
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
    int fnbr = ival;
    if (fnbr < 1 || fnbr > MAXOPENFILES || !fileTable[fnbr].inUse) {
        MMBasic_Error(ERR_FILE_IO, "File not open");
        return;
    }
    fileTable[fnbr].file.flush();
}

// BACKLIGHT - Set LCD backlight brightness
void MMBasic_CmdBacklight(void) {
    char *line = currentLine;
    while (*line == ' ') line++;
    int itype, ival;
    float fval;
    char *sval;
    if (MMBasic_EvaluateExpression(&line, &itype, &ival, &fval, &sval)) return;
    int brightness = ival;
    if (brightness < 0) brightness = 0;
    if (brightness > 100) brightness = 100;
    M5Cardputer.Display.setBrightness(brightness * 255 / 100);
}

// SETTICK - Set periodic interrupt
void MMBasic_CmdSettick(void) {
    HAL_Display_Println("SETTICK not yet implemented");
}

// WATCHDOG - Hardware watchdog
void MMBasic_CmdWatchdog(void) {
    HAL_Display_Println("WATCHDOG not yet implemented");
}

// CPU - Set CPU speed
void MMBasic_CmdCpu(void) {
    HAL_Display_Println("CPU not yet implemented");
}

// MEMORY - Show memory info
void MMBasic_CmdMemory(void) {
    char buf[64];
    sprintf(buf, "Free heap: %u bytes", (unsigned int)ESP.getFreeHeap());
    HAL_Display_Println(buf);
    sprintf(buf, "Used vars: %d/%d", varcnt, MAXVARS);
    HAL_Display_Println(buf);
}
