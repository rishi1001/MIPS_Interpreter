#include <bits/stdc++.h>
#include <boost/algorithm/string.hpp>
using namespace std;

int ROW_ACCESS_DELAY = 10;
int COL_ACCESS_DELAY = 2;
const bool OPTIMIZE = true;

struct instruction {
    string func;
    string tag;
    int arg1;
    int arg2;
    int arg3;
    int count;
};

struct request {
    string type;
    int reg;
    int loc;
    int req_cycle;
    int line;
};

struct job {
    string cmd;
    int reg;
    int loc;
    int line;
};

// Various global variables for keeping track of data
int pc;
int line;
int line_number;
int cycles;
int mem[1 << 18];
int regs[1 << 5];

int buff_row;
int row_read, row_write, col_read, col_write;

// Parameters of current job being executed
string curr_cmd;
int curr_end;

// Storing the instructions and jobs
map<int, instruction> mp;
map<string, int> name_regs;
map<int, string> regs_name;
map<string, int> tags;
map<int, int> line_to_number;

/**** Changes after minor started ****/

queue<job> jobs;
deque<request> requests[32];
int unsafe_reg;

string trim(string str, string whitespace = " \t") {
    const auto strBegin = str.find_first_not_of(whitespace);
    if (strBegin == std::string::npos)
        return ""; // no content

    const auto strEnd = str.find_last_not_of(whitespace);
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

bool requests_pending() {
    bool res = false;
    for(int i = 0; i < 32; i++) if(requests[i].size() != 0) res = true;
    return res;
}

void load_request() {
    if(buff_row == -1) {
        int min_cycle = cycles+1, load_reg = -1;

        if (unsafe_reg != -1 && !requests[unsafe_reg].empty()){
            int load_row = requests[unsafe_reg].front().loc/1024;
            for(int i = 0; i < 32; i++) {
                if(requests[i].size() != 0 && requests[i].front().loc/1024 == load_row) {
                    if (requests[i].front().req_cycle < min_cycle){
                        min_cycle = requests[i].front().req_cycle;
                        load_reg = i;
                    }
                }
            }
            if(requests[load_reg].front().type == "lw") {
                job job1 = {"row_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                job job2 = {"col_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                jobs.push(job1);
                jobs.push(job2);
            } else {
                job job1 = {"row_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                job job2 = {"col_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                jobs.push(job1);
                jobs.push(job2);
            }
            requests[load_reg].pop_front();
            if (unsafe_reg != -1 && requests[unsafe_reg].empty()) unsafe_reg = -1;
            return;
        }
        
        for(int i = 0; i < 32; i++) {
            if(requests[i].size() != 0) {
                if (requests[i].front().req_cycle < min_cycle){
                    min_cycle = requests[i].front().req_cycle;
                    load_reg = i;
                }
            }
        }
        if(requests[load_reg].front().type == "lw") {
            job job1 = {"row_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
            job job2 = {"col_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
            jobs.push(job1);
            jobs.push(job2);
        } else {
            job job1 = {"row_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
            job job2 = {"col_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
            jobs.push(job1);
            jobs.push(job2);
        }
        requests[load_reg].pop_front();
        if (unsafe_reg != -1 && requests[unsafe_reg].empty()) unsafe_reg = -1;

    } else {
        bool found = false;
        int min_cycle = cycles+1, load_reg = -1;
        for(int i = 0; i < 32; i++) {
            if(requests[i].size() != 0 && requests[i].front().loc/1024 == buff_row) {
                found = true;
                if (requests[i].front().req_cycle < min_cycle){
                    min_cycle = requests[i].front().req_cycle;
                    load_reg = i;
                }
            }
        }
        if (found) {
            if(requests[load_reg].front().type == "lw") {
                job job1 = {"col_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                jobs.push(job1);
            } else {
                job job1 = {"col_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                jobs.push(job1);
            }
            requests[load_reg].pop_front();
            if (unsafe_reg != -1 && requests[unsafe_reg].empty()) unsafe_reg = -1;
        }
        
        if(!found) {
            if (unsafe_reg != -1 && !requests[unsafe_reg].empty()) {
                int load_row = requests[unsafe_reg].front().loc/1024;
                for(int i = 0; i < 32; i++) {
                    if(requests[i].size() != 0 && requests[i].front().loc/1024 == load_row) {
                        if (requests[i].front().req_cycle<min_cycle){
                            min_cycle=requests[i].front().req_cycle;
                            load_reg=i;
                        }
                    }
                }
                if(requests[load_reg].front().type == "lw") {
                    job job1 = {"row_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                    job job2 = {"row_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                    job job3 = {"col_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                    jobs.push(job1);
                    jobs.push(job2);
                    jobs.push(job3);
                } else {
                    job job1 = {"row_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                    job job2 = {"row_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                    job job3 = {"col_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                    jobs.push(job1);
                    jobs.push(job2);
                    jobs.push(job3);
                }
                requests[load_reg].pop_front();
                if (unsafe_reg != -1 && requests[unsafe_reg].empty()) unsafe_reg = -1;
                return;
            }

            for(int i = 0; i < 32; i++) {
                if(requests[i].size() != 0) {
                    if (requests[i].front().req_cycle < min_cycle){
                        min_cycle = requests[i].front().req_cycle;
                        load_reg = i;
                    }
                }
            }

            if(requests[load_reg].front().type == "lw") {
                job job1 = {"row_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                job job2 = {"row_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                job job3 = {"col_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                jobs.push(job1);
                jobs.push(job2);
                jobs.push(job3);
            } else {
                job job1 = {"row_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                job job2 = {"row_read", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                job job3 = {"col_write", requests[load_reg].front().reg, requests[load_reg].front().loc, requests[load_reg].front().line};
                jobs.push(job1);
                jobs.push(job2);
                jobs.push(job3);
            }
            requests[load_reg].pop_front();
            if (unsafe_reg != -1 && requests[unsafe_reg].empty()) unsafe_reg = -1;
        }
    }
}

// Load a new job for DRAM
void start_job(job j) {
    curr_cmd = j.cmd;
    if(curr_cmd == "row_read") {
        cout << "DRAM row reading initiated for row " << j.loc/1024 << " (line: " << line_to_number[j.line]+1 << ", memory-address: " << j.line*4 << ")" << "\n";
        curr_end = ROW_ACCESS_DELAY + cycles - 1;
    } else if(curr_cmd == "row_write") {
        cout << "DRAM row writing initiated for row " << buff_row << " (line: " << line_to_number[j.line]+1 << ", memory-address: " << j.line*4 << ")" << "\n";
        curr_end = ROW_ACCESS_DELAY + cycles - 1;
    } else if(curr_cmd == "col_read") {
        cout << "DRAM column reading initiated for location " << j.loc << " (line: " << line_to_number[j.line]+1 << ", memory-address: " << j.line*4 << ")" << "\n";
        curr_end = COL_ACCESS_DELAY + cycles - 1;
    } else {
        cout << "DRAM column writing initiated for location " << j.loc << " (line: " << line_to_number[j.line]+1 << ", memory-address: " << j.line*4 << ")" << "\n";
        curr_end = COL_ACCESS_DELAY + cycles - 1;
    }
}

void finish_job(job j) {
    if(curr_cmd == "row_read") {
        buff_row = j.loc/1024;
        cout << "DRAM row reading completed for row " << buff_row << " (line: " << line_to_number[j.line]+1 << ", memory-address: " << j.line*4 << ")" << "\n";
        row_read++;
    } else if(curr_cmd == "row_write") {
        cout << "DRAM row writing completed for row " << buff_row << " (line: " << line_to_number[j.line]+1 << ", memory-address: " << j.line*4 << ")" << "\n";
        row_write++;
    } else if(curr_cmd == "col_read") {
        regs[j.reg] = mem[j.loc/4];
        cout << "DRAM column reading completed for location " << j.loc << " (line: " << line_to_number[j.line]+1 << ", memory-address: " << j.line*4 << ")" << "\n";
        cout << regs_name[j.reg] << " = " << regs[j.reg] << "\n";
        col_read++;
    } else {
        mem[j.loc/4] = regs[j.reg];
        cout << "DRAM column writing completed for location " << j.loc << " (line: " << line_to_number[j.line]+1 << ", memory-address: " << j.line*4 << ")" << "\n";
        cout << j.loc << "-" << j.loc+3 << ": " << mem[j.loc/4] << "\n";
        col_write++;
    }
    curr_cmd = "";
}

/**** Changes after minor finished ****/

// Print various statistics
void print_stats() {
    cout << "\n";
    cout << "Total clock cycles: " << cycles << "\n";
    cout << "\n";

    cout << "Instruction execution count\n";
    cout << "Line number : Count\n";
    for(int i = 0; i < line; i++) {
        cout << line_to_number[i]+1 << ":" << mp[i].count << "\n";
    }
    cout << "\n";

    cout << "Final data values that are updated during execution: \n";
    for(int i = line; i < (1<<18); i++) if(mem[i] != 0) cout << i*4 << "-" << i*4+3 << ": " << mem[i] << "\n";
    cout << "\n";

    cout << "Row buffer updates and column read/write counts\n";
    cout << "Row read: " << row_read << "\n";
    cout << "Row writes: " << row_write << "\n";
    cout << "Col reads: " << col_read << "\n";
    cout << "Col writes: " << col_write << "\n";
}

// Execute a job: Each function call corresponds to one cycle execution
void execute_job() {
    if(curr_cmd == "") {
        if(!jobs.empty()) jobs.pop();
        if(!jobs.empty()) {
            start_job(jobs.front());
        } else if(requests_pending()) {
            load_request();
            start_job(jobs.front());
        }
    }
    if(cycles == curr_end) {
        finish_job(jobs.front());
    }
}

// Create relevant jobs for reading data at loc into register $reg
void read(int loc, int reg, int index) {
    if (!requests[reg].empty()){
        request pre=requests[reg].back();
        if (pre.type=="lw") requests[reg].pop_back();
    }
    requests[reg].push_back({"lw", reg, loc, cycles, index});
}

// Create relevant jobs for writing data at loc from register $reg
void write(int loc, int reg, int index) {
    requests[reg].push_back({"sw", reg, loc, cycles, index});
}

// Execute the instruction stored at index
void execute_ins(int index) {
    cout << "Executing instruction on line " << line_to_number[index]+1 << " (memory location: " << index*4 << ")" << "\n";
    mp[index] = {mp[index].func, mp[index].tag, mp[index].arg1, mp[index].arg2, mp[index].arg3, mp[index].count+1};
    string func = mp[index].func;
    int arg1 = mp[index].arg1;
    int arg2 = mp[index].arg2;
    int arg3 = mp[index].arg3;
    string tag = mp[index].tag;
    if(func == "add") {
        regs[arg1] = regs[arg2] + regs[arg3];
        cout << regs_name[arg1] << " = " << regs[arg1] << "\n";
        pc++;         
    } else if(func == "sub") {
        regs[arg1] = regs[arg2] - regs[arg3];
        cout << regs_name[arg1] << " = " << regs[arg1] << "\n";
        pc++;       
    } else if(func == "mul") {
        regs[arg1] = regs[arg2] * regs[arg3];
        cout << regs_name[arg1] << " = " << regs[arg1] << "\n";
        pc++;
    } else if(func == "beq") {
        if(tags.find(tag) == tags.end()) {
            cout << "Cannot find tag " + tag + "\n";
            exit(1);
        }
        if (regs[arg1] == regs[arg2]) pc = tags[tag];
        else pc++;      
    } else if(func == "bne") {
        if(tags.find(tag) == tags.end()) {
            cout << "Cannot find tag " + tag + "\n";
            exit(1);
        }
        if (regs[arg1] != regs[arg2]) pc = tags[tag];
        else pc++;
    } else if(func == "slt") {
        if (regs[arg2] < regs[arg3]) regs[arg1] = 1;
        else regs[arg1] = 0;
        cout << regs_name[arg1] << " = " << regs[arg1] << "\n";
        pc++;
    } else if(func == "j") {
        if(tags.find(tag) == tags.end()) {
            cout << "Cannot find tag " + tag + "\n";
            exit(1);
        }
        pc = tags[tag];            
    } else if(func == "lw") {
        int loc = arg2 + regs[arg3];
        if(loc >= 0 && loc < line) {
            cout << "Attempting to read instructions, Exiting\n";
            exit(1);
        } else if(loc >= (1 << 18) || loc < 0) {
            cout << "Out of bounds memeory location\n";
            exit(1);
        }
        if(loc%4 != 0) {
            cout << "Read location must be multiple of 4 on line " << pc+1 << "\n";
            exit(1);
        }
        read(loc, arg1, index);
        // regs[arg1] = mem[loc/4];
        pc++;
    } else if(func == "sw") {
        int loc = arg2 + regs[arg3];
        if(loc >= 0 && loc < line) {
            cout << "Attempting to overwrite instructions, Exiting\n";
            exit(1);
        } else if(loc >= (1 << 18) || loc < 0) {
            cout << "Out of bounds memeory location\n";
            exit(1);
        }
        if(loc%4 != 0) {
            cout << "Write location must be multiple of 4 on line " << pc+1 << "\n";
            exit(1);
        }
        write(loc, arg1, index);
        // mem[loc/4] = regs[arg1];
        pc++;
    } else if(func == "addi") {
        regs[arg1] = regs[arg2] + arg3;
        cout << regs_name[arg1] << " = " << regs[arg1] << "\n";
        pc++;
    }
}

bool safe_register(int reg) {
    if(!jobs.empty() && jobs.front().reg == reg) return false;
    if(requests[reg].size() != 0) return false;
    return true;
}

// Checks if an instruction is safe for execution
bool is_safe(int index) {
    if(!OPTIMIZE) {
        for(int i = 0; i < 32; i++) if(!safe_register(i)) return false;
        return true;
    }

    string func = mp[index].func;
    int arg1 = mp[index].arg1;
    int arg2 = mp[index].arg2;
    int arg3 = mp[index].arg3;

    if(func == "lw" || func == "sw") return true;
    if(func == "j") return true;
    if (!safe_register(arg1)){
        unsafe_reg = arg1;
        return false;
    }
    if (!safe_register(arg2)){
        unsafe_reg = arg2;
        return false;
    }
    if(func == "addi" || func=="bne" || func=="beq") return true;
    else {
        if (!safe_register(arg3)){
            unsafe_reg = arg3;
            return false;
        }
        return true;
    }
}

// Run the program
void run_program() {
    while(pc != line || curr_cmd != "" || !(jobs.empty() || (jobs.size() == 1 && curr_cmd == "")) || requests_pending()) {
        cycles++;
        cout << "Cycle: " << cycles << "\n";
        execute_job();
        if(pc != line && is_safe(pc)) execute_ins(pc);

        cout << "Register values: " << "\n";
        for(int i = 0; i < 32; i++) {
            cout << regs[i] << " ";
        }
        cout << "\n" << "\n";
    }
}

// Process each instruction (mainly check for syntax errors)
void process_instruction(vector<string> tokens) {
    regex r("\\$r([0-9]|1[0-9]|2[0-9]|3[0-1])");
    regex r1("([0-9]*|-[0-9]*)\\(\\$r([0-9]|1[0-9]|2[0-9]|3[0-1])\\)");
    regex r2("[0-9]*|-[0-9]*");

    int size = tokens.size();
    if(size < 2 || size > 4) {
        cout << "Number of tokens in the instructions must be between two and four on line " << line_to_number[line]+1 << "\n";
        exit(1);
    }

    string func = tokens[0];
    int arg1 = -1, arg2 = -1, arg3 = -1, count = 0;
    string tag = "";
    if(func == "add") {
        if(size != 4) {
            cout << "Three arguments must be provided to add instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end() && name_regs.find(tokens[3]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            arg3 = name_regs[tokens[3]];
        } else {
            cout << "All arguments to add must be registers on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
    } else if(func == "sub") {
        if(size != 4) {
            cout << "Three arguments must be provided to sub instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end() && name_regs.find(tokens[3]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            arg3 = name_regs[tokens[3]];
        } else {
            cout << "All arguments to sub must be registers on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
    } else if(func == "mul") {
        if(size != 4) {
            cout << "Three arguments must be provided to mul instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end() && name_regs.find(tokens[3])!=name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            arg3 = name_regs[tokens[3]];
        } else {
            cout << "All arguments to mul must be registers on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
    } else if(func == "beq") {
        if(size != 4) {
            cout << "Three arguments must be provided to beq instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            tag = tokens[3];
        } else {
            cout << "First two arguments to beq must be registers on line and third must be memory location on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
    } else if(func == "bne") {
        if(size != 4) {
            cout << "Three arguments must be provided to bne instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            tag = tokens[3];
        } else {
            cout << "First two arguments to bne must be registers on line and third must be memory location on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
    } else if(func == "slt") {
        if(size != 4) {
            cout << "Three arguments must be provided to slt instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end() && name_regs.find(tokens[3]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            arg3 = name_regs[tokens[3]];
        } else {
            cout << "All arguments to slt must be registers on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }        
    } else if(func == "j") {
        if(size != 2) {
            cout << "One argument must be provided to j instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        tag = tokens[1];

    } else if(func == "lw") {
        if(size != 3) {
            cout << "Two arguments must be provided to lw instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            int op = tokens[2].find("(");
            if(op != 0) {
                if (!regex_match(tokens[2].substr(0,op), r2)){
                    cout << "First argument must be register and second must be memory on line " << line_to_number[line]+1 << "\n";
                    exit(1);
                }
                try{
                    arg2 = stoi(tokens[2].substr(0,op));
                }
                catch(std::out_of_range& e){
                    cout<<"Value is out of range on line "<<line_to_number[line]+1<<"\n";
                    exit(1);
                }
            } else arg2 = 0;
            if (name_regs.find(tokens[2].substr(op+1, tokens[2].size()-op-2)) == name_regs.end()){
                cout << "First argument must be register and second must be memory on line " << line_to_number[line]+1 << "\n";
                exit(1);
            }
            arg3 = name_regs[tokens[2].substr(op+1, tokens[2].size()-op-2)];
        } else {
            cout << "First argument must be register and second must be memory on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
    } else if(func == "sw") {
        if(size != 3) {
            cout << "Two arguments must be provided to sw instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            int op = tokens[2].find("(");
            if(op != 0) {
                if (!regex_match(tokens[2].substr(0,op), r2)){
                    cout << "First argument must be register and second must be memory on line " << line_to_number[line]+1 << "\n";
                    exit(1);
                }
                try{
                    arg2 = stoi(tokens[2].substr(0,op));
                }
                catch(std::out_of_range& e){
                    cout<<"Value is out of range on line "<<line_to_number[line]+1<<"\n";
                    exit(1);
                }
            } else arg2 = 0;
            if (name_regs.find(tokens[2].substr(op+1, tokens[2].size()-op-2)) == name_regs.end()){
                cout << "First argument must be register and second must be memory on line " << line_to_number[line]+1 << "\n";
                exit(1);
            }
            arg3 = name_regs[tokens[2].substr(op+1, tokens[2].size()-op-2)];
        } else {
            cout << "First argument must be register and second must be memory on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }       
    } else if(func == "addi") {
        if(size != 4) {
            cout << "Three arguments must be provided to addi instruction on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }
        if(name_regs.find(tokens[1])!=name_regs.end() && name_regs.find(tokens[2])!=name_regs.end() && regex_match(tokens[3], r2)) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            try{
                arg3 = stoi(tokens[3]);
            }
            catch(std::out_of_range& e){
                cout<<"Value is out of range on line "<<line_to_number[line]+1<<"\n";
                exit(1);
            }
        } else {
            cout << "First two arguments to add must be registers on line and last must be integer on line " << line_to_number[line]+1 << "\n";
            exit(1);
        }        
    } else {
        cout << "Unrecognised function: " + func + " on line " << line_to_number[line]+1 << "\n";
        exit(1);
    }

    instruction ins = {func, tag, arg1, arg2, arg3, count};
    mp[line] = ins;
    mem[line] = line;
}

// Read and tokenize the contents of input file
void read_file(string file_name) {
    ifstream file(file_name);
    string ins;
    while(getline(file, ins)) {
        vector<string> tokens;
        vector<string> tokens_temp;
        ins = trim(ins);
        boost::split(tokens_temp, ins, boost::is_any_of(", "));
        for(string token : tokens_temp) if(token != "") tokens.push_back(token);
        line_to_number[line] = line_number;
        if(tokens.size() == 1) {
            string func = tokens[0];
            int op = func.find(":");
            if (op != -1) {
                string tag = func.substr(0,op);
                tags[tag] = line;
            } else {
                cout << "Unrecognised function: " + func + " on line " << line_to_number[line]+1 << "\n";
                exit(1);
            }
        } else if(tokens.size() > 1) {
            process_instruction(tokens);
            line++;
        }
        line_number++;
    }
}

// Initialise various global variables
void initialise() {
    pc = 0;
    line = 0;
    line_number = 0;
    cycles = 0;

    curr_cmd = "";
    curr_end = -1;
    
    buff_row = -1;
    row_read = 0, row_write = 0, col_read = 0, col_write = 0;
    for(int i = 0; i < (1<<18); i++) mem[i] = 0;
    for(int i = 0; i < (1<<5); i++) regs[i] = 0;
    name_regs = {{"$zero", 0}, {"$at", 1}, {"$v0", 2}, {"$v1", 3}, {"$a0", 4}, {"$a1", 5}, {"$a2", 6}, {"$a3", 7}, {"$t0", 8}, {"$t1", 9}, {"$t2", 10}, {"$t3", 11}, {"$t4", 12}, {"$t5", 13}, {"$t6", 14}, {"$t7", 15}, {"$s0", 16}, {"$s1", 17}, {"$s2", 18}, {"$s3", 19}, {"$s4", 20}, {"$s5", 21}, {"$s6", 22}, {"$s7", 23}, {"$t8", 24}, {"$t9", 25}, {"$k0", 26}, {"$k1", 27}, {"$gp", 28}, {"$sp", 29}, {"$fp", 30}, {"$ra", 31}};
    regs_name = {{0, "$zero"}, {1, "$at"}, {2, "$v0"}, {3, "$v1"}, {4, "$a0"}, {5, "$a1"}, {6, "$a2"}, {7, "$a3"}, {8, "$t0"}, {9, "$t1"}, {10, "$t2"}, {11, "$t3"}, {12, "$t4"}, {13, "$t5"}, {14, "$t6"}, {15, "$t7"}, {16, "$s0"}, {17, "$s1"}, {18, "$s2"}, {19, "$s3"}, {20, "$s4"}, {21, "$s5"}, {22, "$s6"}, {23, "$s7"}, {24, "$t8"}, {25, "$t9"}, {26, "$k0"}, {27, "$k1"}, {28, "$gp"}, {29, "$sp"}, {30, "$fp"}, {31, "$ra"}};
    
    unsafe_reg = -1;
}

int main(int argc, char* argv[]) {
    if(argc!=2 && argc!=4) {
        cout << "Please execute the program as ./program <file_name> ROW_ACCESS_DELAY COLUMN_ACCESS_DELAY" << "\n";
        exit(1);
    }
    if (argc==4){
        ROW_ACCESS_DELAY=stoi(argv[2]);
        COL_ACCESS_DELAY=stoi(argv[3]);
    }

    initialise();
    read_file(argv[1]);
    run_program();
    print_stats();
}