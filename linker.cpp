/*
This program should run on the compiler's versions which are after 5.2.0 (GCC)
Please also enable C++11 in GCC as using the following command: "g++ -std=c++11 -g linker.cpp -o linker"
*/

#include <string>
#include <typeinfo>
#include <iomanip>
#include <iostream>
#include <cstdio>
#include <regex>
using namespace std;

FILE *inputFile;
char* inputFilePath;
char currentChar;
char nextChar;
char buffer[100];
int moduleCount = 0;
int moduleBaseAddr = 0;
int charOffset = 0;
int tokenOffset = 1;
int charLineNumber = 1;
int tokenLineNumber = 1;
int instDisplayCount = 0;
int tokenBeginPos = 0;
int SymbolLengthMax = 16; //accepted symbols should be upto 16 characters long
int countMax = 16; //a usel/def list should support 16 definitions
int memorySizeMax = 512;
bool secondPassFlag = false;
bool isFindAllDefSymbol = false;
regex numeric("[0-9]+");
regex validSymbol("([A-Z]|[a-z])([A-Z]|[a-z]|[0-9])*");

//I do not store pares tokens/deflist/uselist from the first pass, but only store symbol table & list of modules
map<string,int> symbolTable;
map<string,int> symbolTableRel;
map<string,int> symbolModule;
map<string,int> symbolUseCount;
map<string,int> symbolDefineTime;
map<string,bool> symbolUsageState;
vector<string> useListOfModule; //only be used in the second pass


//print the parsing error
void __parseerror(int errcode) {
    const char* errstr[] = {
        "NUM_EXPECTED",
        "SYM_EXPECTED",
        "ADDR_EXPECTED",
        "SYM_TOO_LONG",
        "TOO_MANY_DEF_IN_MODULE",
        "TOO_MANY_USE_IN_MODULE",
        "TOO_MANY_INSTR",
    };
    cout << "Parse Error line " << tokenLineNumber << " offset " << tokenOffset << ": " 
         << errstr[errcode] << endl;
    exit(1); //only Parse Error will terminate the process
}

//print the symbol table
void showSymbolTable() { //After pass one, print the symbol table (including errors related to it)
    cout << "Symbol Table" << endl;
    //cout << "max size: " << symbolTable.max_size() << endl; //Symbol Table can support at least 256 symbols
    for (auto const & i : symbolTable) {
        cout << i.first << "=" << i.second;

        if (symbolDefineTime[i.first] > 1) { //when find a symbol is defined multiple times
            cout << " Error: This variable is multiple times defined; first value used";
        }
        cout << endl;
    }
    cout << endl;

    charLineNumber = 1;
    tokenLineNumber = 1;
}

//get token
string getToken() {
    string token = "";

    currentChar = nextChar;
    nextChar = fgetc(inputFile);
    charOffset++;

    while (currentChar == ' ' || currentChar == '\n' || currentChar == '\t'|| currentChar == '\0') {
        currentChar = nextChar;
        nextChar = fgetc(inputFile);
        charOffset++;
        if (currentChar == '\n' && !feof(inputFile)) {charLineNumber++; charOffset = 0;}
    }

    if(feof(inputFile)) {
        tokenOffset = charOffset;
        return token;
    }

    tokenOffset = charOffset;
    tokenLineNumber = charLineNumber;

    //token delimiters are “”, “\t”, ”\n"
    while(currentChar != ' ' && currentChar != '\t' && currentChar != '\n' && currentChar != '\0' && !feof(inputFile)){
        token = token + currentChar;

        currentChar = nextChar;
        nextChar = fgetc(inputFile);
        charOffset++;
        if (currentChar == '\n' && !feof(inputFile)) {charLineNumber++; charOffset = 0;}
    }

    while (nextChar == ' ' || nextChar == '\n' || nextChar == '\t' || nextChar == '\0') {
        currentChar = nextChar;
        nextChar = fgetc(inputFile);
        charOffset++;
        if (currentChar == '\n' && !feof(inputFile)) {charLineNumber++; charOffset = 0;}
    }

    return token;
}

//read symbol
string readSymbol() {
    if (feof(inputFile)) {
        __parseerror(1);  //raise the "sym expected" error
    }

    string token = getToken();

    if (token.length() > SymbolLengthMax) { //valid symbols can be up to 16 characters
        __parseerror(3);  //raise the "sym too long" error
    }

    //symbols always begin with alpha characters followed by optional alphanumerical characters
    if (!regex_match(token, validSymbol) && token != "") { //the empty token case here is for the space before line breaks
        __parseerror(1);  //raise the "sym expected" error
    }

    return token;
}

//read integer
int readInteger() {
    int val = 0;
    string token = getToken();

    if (regex_match(token, numeric)){ //integer are decimal based.
        val = stoi(token);
    } else { //if the token is not integer
        __parseerror(0); //raise the "number expect" error
    }

    return val;
}

//handle the case when in the end of file
void inEndOfFile(int count, string mode) {
    if (count == 0) return;
    else {
        tokenOffset = charOffset; //move the tokenOffset to the end when meeting the end of file
        tokenLineNumber = charLineNumber; //move the tokenLineNumber to the end when meeting the end of file
        if (mode == "Use" || mode == "Use") __parseerror(1); //raise the "sym expected" error
        if (mode == "Inst") __parseerror(2); //raise the "addr expected" error
    }
}

//read each instruction item
void readEachInst(int codeCount) {
    string type = getToken(); //read inst's type

    if (type == "") __parseerror(1); //symbol actual amount is less than code count

    // if type not belongs to A/E/I/R
    if(type != "A" && type != "E" && type != "I" && type != "R"){
        __parseerror(2); //raise the "addr expected" error
    }

    int instr = readInteger();

    string instErrorMessage = ""; //read inst's instr

    if (secondPassFlag) {
        int opcode = instr / 1000;
        int operand = instr % 1000;
        if (opcode >= 10) { //an illegal opcode is encountered, instr is an upto 4-digit instruction
            instr = 9999; //convert the <opcode, operand> to 9999

            if (type == "I") { //an illegal (I) immediate value is encountered
                instErrorMessage = "Error: Illegal immediate value; treated as 9999";
            }
            else {
                instErrorMessage = "Error: Illegal opcode; treated as 9999";
            }
        }
        else {
            if (type == "A") {
                if (operand > memorySizeMax) { //absolute operand can’t be “>=“ the machine size (512)
                    instr = instr - operand; //an absolute address exceeds the size of the machine, print error and use the absolute value zero
                    instErrorMessage = "Error: Absolute address exceeds machine size; zero used";
                }
            }

            if (type == "E") {
                if (operand >= stoi(useListOfModule[0])) { //compare operand with use count
                    instErrorMessage = "Error: External address exceeds length of uselist; treated as immediate";
                }
                else {
                    string symbol = useListOfModule[operand+1]; //find the symbol which is used in an E type instruction of current module
                    symbolUsageState[symbol] = true;

                    if (symbolTable.find(symbol) == symbolTable.end()) { //symobl is not found from the Symbol Table
                        instErrorMessage = "Error: " + symbol + " is not defined; zero used";
                        instr = opcode * 1000;
                        symbolUseCount[symbol] += 1;
                    } else { //found
                        //(E) External operand K represents the Kth symbol in the use list, using 0-based counting
                        //identify which global address the symbol is assigned and then replace the operand with that global address.
                        instr = opcode * 1000 + symbolTable[symbol];
                        symbolUseCount[symbol] += 1;
                    }
                }
            }

            if (type == "R") {
                if (operand > codeCount) {
                    instr = opcode * 1000 + moduleBaseAddr;
                    instErrorMessage = "Error: Relative address exceeds module size; zero used";
                }
                else {
                    instr = instr + moduleBaseAddr; // resolve module relative addressing by assigning global address
                }
            }
        }

        cout << setfill('0') << right << setw(3) << instDisplayCount << ": " << setfill('0') << right << setw(4) << instr;
        if (instErrorMessage != "") cout << " " << instErrorMessage;
        cout << endl;

        instDisplayCount++;
    } 
}

//read the instruction list
void readInst(){
    int codeCount = readInteger();  //read code's count
    int codeCountSign = codeCount;

    if (moduleBaseAddr + codeCount > memorySizeMax) __parseerror(6); //raise the "too many instr" error (total num_instr exceeds memory size (512))
    if (feof(inputFile)) inEndOfFile(codeCount, "Inst"); //handle the case when in the end of file

    while (codeCountSign > 0) {
        if (feof(inputFile)) break;
        readEachInst(codeCount);
        codeCountSign--;
    }

    if (!secondPassFlag) {
        for (auto const & i : symbolTableRel) {
            if (symbolModule[i.first] == moduleCount && symbolTableRel[i.first] > codeCount) {
                cout << "Warning: Module " << moduleCount << ": " << 
                    i.first << " too big " << symbolTableRel[i.first] << " (max=" << (codeCount-1) << ") assume zero relative" << endl;
                symbolTable[i.first] = symbolTable[i.first] - symbolTableRel[i.first];
                symbolTableRel[i.first] = 0;
            }
        }
    }
    else {
        for (auto const & i : symbolUsageState) {
            if (!i.second) {
                cout << "Warning: Module " << moduleCount << ": " << i.first << " appeared in the uselist but was not actually used" << endl;
            }
        }
        symbolUsageState.clear();
    }

    moduleBaseAddr += codeCount; //codecount is the length of the module
}

//read the use list
void readUse(){
    int useCount = readInteger(); //read use's count
    int useCountSign = useCount;

    if (useCount > countMax) __parseerror(5); //raise the "too many use in module" error (support 16 definitions)
    if (feof(inputFile)) inEndOfFile(useCount, "Use"); //handle the case when in the end of file

    if(secondPassFlag) useListOfModule.push_back(to_string(useCount)); //store use's count into Current Use List vetor

    while (useCountSign > 0) {
        string symbol = readSymbol();
        useCountSign--;

        if(secondPassFlag) useListOfModule.push_back(symbol); //store use's symbol into Current Use List vetor
    }

    if(secondPassFlag){
        for (int x = 1; x <= stoi(useListOfModule[0]); x++) {
            symbolUsageState[useListOfModule[x]] = false; //initiate symbolUsageState map
        }
    }
}

void createSymbol(string symbol, int value) {
    if(symbolTable.find(symbol) == symbolTable.end()) { //symbol is not found from Symbol Table
        //I do not store pares tokens/deflist/uselist from the first pass, but only store symbol table & list of modules
        symbolTable[symbol] = moduleBaseAddr + value; //store symbol with its absolute position
        symbolTableRel[symbol] = value; //store symbol with its relative position
        symbolModule[symbol] = moduleCount; //store symbol with the module which it is defined in
        symbolUseCount[symbol] = 0; //initiate the usage number for each symbol in each module
        symbolDefineTime[symbol] = 1; //record the definition time for each symbol
    }
    else { //symbol is found from Symbol Table
        symbolDefineTime[symbol]++;
    }
}

//read each def item
void readEachDef() {
    string symbol = readSymbol(); //read def's symbol
    int value = readInteger(); //read def's value

    if(!secondPassFlag) {
        createSymbol(symbol, value); //create Symbol Table when in the first pass
    }
}

// read the def list
void readDef() {
    int defCount = readInteger(); //read def's count
    int defCountSign = defCount;

    if (defCount > countMax) __parseerror(4); //raise the "too many def in module" error (support 16 definitions)
    if (feof(inputFile)) inEndOfFile(defCount, "Def"); //handle the case when in the end of file

    while (defCountSign > 0) {
        readEachDef();
        defCountSign--;
    }
}

//read new module
void readModule() {
    readDef();
    readUse();
    readInst();
    useListOfModule.clear();
}

//the second pass
void secondPass() {
    secondPassFlag = true;
    inputFile = fopen(inputFilePath,"r");

    //reset module base addr & module count
    moduleBaseAddr = 0;
    moduleCount = 0;

    cout << "Memory Map" << endl;
    if (inputFile != NULL) {
        nextChar = fgetc(inputFile);
        while (!feof(inputFile)) {
            moduleCount++;
            readModule();
        }
    }
    else perror ("Something wrong for opening the file");

    cout<<endl;
    for (auto const & i : symbolUseCount) {
        if(i.second==0){
            cout<<"Warning: Module "<< symbolModule[i.first] <<": " << i.first<<" was defined but never used"<<endl;
        }
    }
    fclose(inputFile);
}

//the first pass
void firstPass() {
    if (inputFile != NULL) {
        nextChar = fgetc(inputFile);
        while (!feof(inputFile)) {
            moduleCount++; //module counting start at 1
            readModule(); //get rid of extra spaces
        }
    } 
    else perror ("Something wrong for opening the file");
    fclose(inputFile);
}

//main function
int main(int argc, char* argv[]) {
    for (int x=1; x<argc; x++) {
        inputFilePath = argv[x];
        inputFile = fopen(inputFilePath,"r");

        firstPass();
        showSymbolTable(); //after pass one, print the symbol table
        secondPass();
    }
    cout << endl;
}