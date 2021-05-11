#include <bits/stdc++.h>
#include <boost/algorithm/string.hpp>
using namespace std;

// TODO : lw $t0, 1000($t1)
//        add $t0, $t2, $t3 here first lw is reduntant?   
// TODO : print total cycles lost in MRM
// TODO : make graph while tuning the paramters, testing this on typical programs like sort, loop, maybe recursion, SPEC(benchmark), similar programs with slightly diff params
// TODO : print pc[i] for each cpu
// TODO : select request based on past.
// TODO : delay in forwarding(depending upon queue size) and also delay in redunctant request removing.
// TODO : if queue size is more then whenever we add new request to queue, again compare in the queue. (like this is drawback of having large queue)
// TODO : Intead of showing empty clock cycles terminate the code when we are done. Also when there is an error in all the cpus, termination should happen.
// TODO : Think for architecture of everything.

int ROW_ACCESS_DELAY = 10;
int COL_ACCESS_DELAY = 2;
const bool OPTIMIZE = true;
const bool VERBROSE = true;
const int MAX_CPUS = 16;
const int MAX_MRM_SIZE = 10; // TODO: Tune this paramater
const int REQUEST_LOADING_DELAY = MAX_MRM_SIZE / 2;

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
    int cpu;
};

struct job {
    string cmd;
    int reg;
    int loc;
    int line;
    int cpu;
};

// Various global variables for keeping track of data
int pc[MAX_CPUS];
int line[MAX_CPUS];
int line_number[MAX_CPUS];
int cycles[MAX_CPUS];
bool valid[MAX_CPUS];
int offset[MAX_CPUS];
int mem[1 << 18];
int regs[MAX_CPUS][1 << 5];
int cpus, m;
int tot_cycles;
int tot_lines;
int tot_instructions;               

int block_size;

int buff_row;
int row_read, row_write, col_read, col_write;

// Parameters of current job being executed
string curr_cmd;
int curr_end;

// Storing the instructions and jobs
map<int, instruction> mp[MAX_CPUS];
map<string, int> name_regs;
map<int, string> regs_name;
map<string, int> tags[MAX_CPUS];
map<int, int> line_to_number[MAX_CPUS];

queue<job> jobs;
deque<request> requests[MAX_CPUS][32];
vector<request> all_requests;
int unsafe_reg[MAX_CPUS];

bool dram_writing_flag;
bool dram_loading;
int count_loading_cycles;
bool buffer_bottleneck;                        

// TODO: Check this function
int get_request_loading_delay() {
    return all_requests.size() / 2;
}

string trim(string str, string whitespace = " \t") {
    const auto strBegin = str.find_first_not_of(whitespace);
    if (strBegin == std::string::npos)
        return ""; // no content

    const auto strEnd = str.find_last_not_of(whitespace);
    const auto strRange = strEnd - strBegin + 1;

    return str.substr(strBegin, strRange);
}

bool is_top_request(struct request req) {
    int min_req_cycle = tot_cycles;
    for(request r : all_requests) {
        if(r.loc == req.loc) min_req_cycle = min(min_req_cycle, r.req_cycle);
    }
    return min_req_cycle == req.req_cycle;
}

void remove_request(struct request req) {
    for(auto it = all_requests.begin(); it != all_requests.end(); ++it) {
        if(it->req_cycle == req.req_cycle) {
            all_requests.erase(it);
            break;
        }
    }
}

bool requests_pending() {
    return all_requests.size() != 0;
}

void load_request_helper(struct request req, int type , int curr_cpu) {
    // if(req.type == "forward") {
    //     regs[req.cpu][req.reg] = mem[req.loc/4];
    //     if(VERBROSE) cout << "DRAM request forwarded" << " (line: " << line_to_number[req.cpu][req.line]+1 << ", CPU: " << req.cpu+1 << ")" << "\n";
    //     if(VERBROSE) cout << regs_name[req.reg] << " = " << regs[req.cpu][req.reg] << "\n";
    //     dram_writing_flag = true;
    //     tot_instructions++;
    //     return;
    // }

    if(type >= 3) jobs.push({"row_write", req.reg, req.loc, req.line, curr_cpu});
    if(type >= 2) jobs.push({"row_read", req.reg, req.loc, req.line, curr_cpu});
    if(req.type == "lw") jobs.push({"col_read", req.reg, req.loc, req.line, curr_cpu});
    else jobs.push({"col_write", req.reg, req.loc, req.line, curr_cpu});
}

void load_request() {
    int load_cpu = -1, load_reg = -1, type = -1;
    bool found = false;

    for(int i = 0; i < cpus; i++) {
        for(int j = 0; j < 32; j++) {
            if(requests[i][j].size() != 0 && requests[i][j].front().loc/1024 == buff_row && is_top_request(requests[i][j].front())) {
                found = true;
                load_cpu = i;
                load_reg = j;
            }
        }
    }

    if(found) type = 1;
    else {
        if(buff_row == -1) type = 2;
        else type = 3;

        int load_row = -1;
        for(int i = 0; i < cpus; i++) {
            if(unsafe_reg[i] != -1 && !requests[i][unsafe_reg[i]].empty()) {
                load_row = requests[i][unsafe_reg[i]].front().loc/1024;
                for(int i = 0; i < cpus; i++) {
                    for(int j = 0; j < 32; j++) {
                        if(requests[i][j].size() != 0 && requests[i][j].front().loc/1024 == load_row && is_top_request(requests[i][j].front())) {
                            found = true;
                            load_cpu = i;
                            load_reg = j;
                        }
                    }
                }
            }
        }

        if(!found) {
            for(int i = 0; i < cpus; i++) {
                for(int j = 0; j < 32; j++) {
                    if(requests[i][j].size() != 0 && is_top_request(requests[i][j].front())) {
                        load_cpu = i;
                        load_reg = j;
                    }
                }
            }
        }  
    }

    load_request_helper(requests[load_cpu][load_reg].front(), type, load_cpu);
    remove_request(requests[load_cpu][load_reg].front());
    requests[load_cpu][load_reg].pop_front();
    if(unsafe_reg[load_cpu] != -1 && requests[load_cpu][unsafe_reg[load_cpu]].empty()) unsafe_reg[load_cpu] = -1;
}

// Load a new job for DRAM
void start_job(job j) {
    curr_cmd = j.cmd;
    if(curr_cmd == "row_read") {
        if(VERBROSE) cout << "DRAM row reading initiated for row " << j.loc/1024 << " (line: " << line_to_number[j.cpu][j.line]+1 << ", CPU: " << j.cpu+1 << ")" << "\n";
        curr_end = ROW_ACCESS_DELAY + tot_cycles - 1;
    } else if(curr_cmd == "row_write") {
        if(VERBROSE) cout << "DRAM row writing initiated for row " << buff_row << " (line: " << line_to_number[j.cpu][j.line]+1 << ", CPU: " << j.cpu+1 << ")" << "\n";
        curr_end = ROW_ACCESS_DELAY + tot_cycles - 1;
    } else if(curr_cmd == "col_read") {
        if(VERBROSE) cout << "DRAM column reading initiated for location " << j.loc << " (line: " << line_to_number[j.cpu][j.line]+1 << ", CPU: " << j.cpu+1 << ")" << "\n";
        curr_end = COL_ACCESS_DELAY + tot_cycles - 1;
    } else {
        if(VERBROSE) cout << "DRAM column writing initiated for location " << j.loc << " (line: " << line_to_number[j.cpu][j.line]+1 << ", CPU: " << j.cpu+1 << ")" << "\n";
        curr_end = COL_ACCESS_DELAY + tot_cycles - 1;
    }
}

void finish_job(job j) {
    if(curr_cmd == "row_read") {
        buff_row = j.loc/1024;
        if(VERBROSE) cout << "DRAM row reading completed for row " << buff_row << " (line: " << line_to_number[j.cpu][j.line]+1 << ", CPU: " << j.cpu+1 << ")" << "\n";
        row_read++;
    } else if(curr_cmd == "row_write") {
        if(VERBROSE) cout << "DRAM row writing completed for row " << buff_row << " (line: " << line_to_number[j.cpu][j.line]+1 << ", CPU: " << j.cpu+1 << ")" << "\n";
        row_write++;
    } else if(curr_cmd == "col_read") {
        regs[j.cpu][j.reg] = mem[j.loc/4];
        if(VERBROSE) cout << "DRAM column reading completed for location " << j.loc << " (line: " << line_to_number[j.cpu][j.line]+1 << ", CPU: " << j.cpu+1 << ")" << "\n";
        if(VERBROSE) cout << regs_name[j.reg] << " = " << regs[j.cpu][j.reg] << "\n";
        dram_writing_flag = true;
        col_read++;
        tot_instructions++;
    } else {
        mem[j.loc/4] = regs[j.cpu][j.reg];
        if(VERBROSE) cout << "DRAM column writing completed for location " << j.loc << " (line: " << line_to_number[j.cpu][j.line]+1 << ", CPU: " << j.cpu+1 << ")" << "\n";
        if(VERBROSE) cout << j.loc << "-" << j.loc+3 << ": " << mem[j.loc/4] << "\n";
        col_write++;
        tot_instructions++;
    }
    curr_cmd = "";
}

// Print various statistics
void print_stats() {
    cout << "\n";
    cout << "Total clock cycles: " << tot_cycles << "\n";
    cout << "Total instructions executed: " << tot_instructions << "\n";
    cout << "\n";

    cout << "Instruction execution count\n";
    cout << "Line number : Count\n";
    for(int i = 0; i < cpus; i++) {
        cout << "CPU: " << i+1 << "\n";
        for(int j = 0; j < line[i]; j++) {
            cout << line_to_number[i][j]+1 << ":" << mp[i][j].count << "\n";
        }
        cout << "\n";
    }

    cout << "Final data values that are updated during execution: \n";
    for(int i = 0; i < (1<<18); i++) if(mem[i] != 0) cout << i*4 << "-" << i*4+3 << ": " << mem[i] << "\n";  
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
            if(count_loading_cycles >= get_request_loading_delay()) {
                dram_loading = true;
            }
            if(dram_loading) {
                load_request();
                if(!jobs.empty()) start_job(jobs.front());
                count_loading_cycles = 0;
                dram_loading = false;
            } else {
                cout << "Still selecting which job to perform" << "\n";
            }
        }
    }
    if(tot_cycles == curr_end) {
        finish_job(jobs.front());
    }
}

// Create relevant jobs for reading data at loc into register $reg
void read(int loc, int reg, int index, int curr_cpu) {
    // Checking for possile forwarding
    bool found = false;
    int max_cycle = -1;
    int max_reg = -1;
    for(auto it = all_requests.begin(); it != all_requests.end(); it++) {
        if(it->loc == loc && it->cpu == curr_cpu) {
            if(it->req_cycle > max_cycle) {
                if(it->type == "sw") {
                    found = true;
                    max_cycle = it->req_cycle;
                    max_reg = it->reg;
                }
            }
        }
    }

    if(!jobs.empty()) {              // ? change anything here
        if(jobs.back().cpu == curr_cpu && jobs.back().loc == loc && jobs.back().cmd == "col_write") {
            found = true;
            max_reg = jobs.back().reg;
        }
    }

    if(found) {
        if(max_reg == reg) {
            cout << "Current instruction is ignored due to redundancy\n";
            tot_instructions++;
            return;
        }
        regs[curr_cpu][reg] = regs[curr_cpu][max_reg];     
        dram_writing_flag=true;
        cout<<"Value forwarded"<<"\n";
        if(VERBROSE) cout << regs_name[reg] << " = " << regs[curr_cpu][reg] << " (CPU " << curr_cpu+1 << ")"  << "\n";
        // requests[curr_cpu][reg].push_back({"forward", reg, loc, tot_cycles, index});
        // all_requests.push_back({"forward", reg, loc, tot_cycles, index, curr_cpu});
        return;
    }

    if(!requests[curr_cpu][reg].empty()){
        request pre = requests[curr_cpu][reg].back();
        if(pre.type == "lw") {
            remove_request(pre);
            requests[curr_cpu][reg].pop_back();
        }
    }
    requests[curr_cpu][reg].push_back({"lw", reg, loc, tot_cycles, index});
    all_requests.push_back({"lw", reg, loc, tot_cycles, index, curr_cpu});
}

// Create relevant jobs for writing data at loc from register $reg
void write(int loc, int reg, int index, int curr_cpu) {
    int max_clock_cycle = -1;
    for(request req : all_requests) {
        if(req.loc == loc) max_clock_cycle = max(max_clock_cycle, req.req_cycle);
    }
    for(auto it = all_requests.begin(); it != all_requests.end(); ++it) { 
        if(it->req_cycle == max_clock_cycle && it->type == "sw" && it->cpu == curr_cpu) {
            all_requests.erase(it);
            for(int i = 0; i < cpus; i++) { 
                for(auto it1 = requests[i][it->reg].begin(); it1 != requests[i][it->reg].end(); ++it1) {
                    if(it1->req_cycle == max_clock_cycle) {
                        requests[i][it->reg].erase(it1);
                        break;
                    }
                }
            }
            break;
        }
    }

    requests[curr_cpu][reg].push_back({"sw", reg, loc, tot_cycles, index, curr_cpu});
    all_requests.push_back({"sw", reg, loc, tot_cycles, index, curr_cpu});
}

// Execute the instruction stored at index
void execute_ins(int index, int curr_cpu) {
    if(VERBROSE) cout << "Executing instruction on line " << line_to_number[curr_cpu][index]+1 << " (memory location: " << index*4 << ") of CPU " << curr_cpu+1 << "\n";
    mp[curr_cpu][index] = {mp[curr_cpu][index].func, mp[curr_cpu][index].tag, mp[curr_cpu][index].arg1, mp[curr_cpu][index].arg2, mp[curr_cpu][index].arg3, mp[curr_cpu][index].count+1};
    string func = mp[curr_cpu][index].func;
    int arg1 = mp[curr_cpu][index].arg1;
    int arg2 = mp[curr_cpu][index].arg2;
    int arg3 = mp[curr_cpu][index].arg3;
    string tag = mp[curr_cpu][index].tag;
    if(func == "add") {
        regs[curr_cpu][arg1] = regs[curr_cpu][arg2] + regs[curr_cpu][arg3];
        if(VERBROSE) cout << regs_name[arg1] << " = " << regs[curr_cpu][arg1] << " (CPU " << curr_cpu+1 << ")" << "\n";
        pc[curr_cpu]++; 
        tot_instructions++;        
    } else if(func == "sub") {
        regs[curr_cpu][arg1] = regs[curr_cpu][arg2] - regs[curr_cpu][arg3];
        if(VERBROSE) cout << regs_name[arg1] << " = " << regs[curr_cpu][arg1] << " (CPU " << curr_cpu+1 << ")" << "\n";
        pc[curr_cpu]++;   
        tot_instructions++;    
    } else if(func == "mul") {
        regs[curr_cpu][arg1] = regs[curr_cpu][arg2] * regs[curr_cpu][arg3];
        if(VERBROSE)  cout << regs_name[arg1] << " = " << regs[curr_cpu][arg1] << " (CPU " << curr_cpu+1 << ")" << "\n";
        pc[curr_cpu]++;
        tot_instructions++;
    } else if(func == "beq") {
        if(tags[curr_cpu].find(tag) == tags[curr_cpu].end()) {
            cout << "Cannot find tag " + tag + "\n";
            valid[curr_cpu] = false;
            return;
        }
        if (regs[curr_cpu][arg1] == regs[curr_cpu][arg2]) pc[curr_cpu] = tags[curr_cpu][tag];
        else pc[curr_cpu]++; 
        tot_instructions++;     
    } else if(func == "bne") {
        if(tags[curr_cpu].find(tag) == tags[curr_cpu].end()) {
            cout << "Cannot find tag " << tag << " (CPU " << curr_cpu+1 << ")" << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if (regs[curr_cpu][arg1] != regs[curr_cpu][arg2]) pc[curr_cpu] = tags[curr_cpu][tag];
        else pc[curr_cpu]++;
        tot_instructions++;
    } else if(func == "slt") {
        if (regs[curr_cpu][arg2] < regs[curr_cpu][arg3]) regs[curr_cpu][arg1] = 1;
        else regs[curr_cpu][arg1] = 0;
        if(VERBROSE) cout << regs_name[arg1] << " = " << regs[curr_cpu][arg1] << " (CPU " << curr_cpu+1 << ")" << "\n";
        pc[curr_cpu]++;
        tot_instructions++;
    } else if(func == "j") {
        if(tags[curr_cpu].find(tag) == tags[curr_cpu].end()) {
            cout << "Cannot find tag " << tag << " (CPU " << curr_cpu+1 << ")" << "\n";
            valid[curr_cpu] = false;
            return;
        }
        pc[curr_cpu] = tags[curr_cpu][tag];
        tot_instructions++;       
    } else if(func == "lw") {
        int loc = arg2 + regs[curr_cpu][arg3];
        if(loc >= block_size || loc < 0) {
            cout << "Out of bounds memeory location" << " (CPU " << curr_cpu+1 << ")" << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(loc%4 != 0) {
            cout << "Read location must be multiple of 4 on line " << line_to_number[curr_cpu][pc[curr_cpu]]+1 << " (CPU " << curr_cpu+1 << ")" << "\n";
            valid[curr_cpu] = false;
            return;
        }
        read(loc+offset[curr_cpu], arg1, index , curr_cpu);
        buffer_bottleneck = true;
        // regs[arg1] = mem[loc/4];
        pc[curr_cpu]++;
    } else if(func == "sw") {
        int loc = arg2 + regs[curr_cpu][arg3];
        if(loc >= block_size || loc < 0) {
            cout << "Out of bounds memeory location" << " (CPU " << curr_cpu+1 << ")" << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(loc%4 != 0) {
            cout << "Read location must be multiple of 4 on line " << line_to_number[curr_cpu][pc[curr_cpu]]+1 << " (CPU " << curr_cpu+1 << ")" << "\n";
            valid[curr_cpu] = false;
            return;
        }
        write(loc+offset[curr_cpu], arg1, index, curr_cpu);
        buffer_bottleneck = true;
        // mem[loc/4] = regs[arg1];
        pc[curr_cpu]++;
    } else if(func == "addi") {
        regs[curr_cpu][arg1] = regs[curr_cpu][arg2] + arg3;
        if(VERBROSE) cout << regs_name[arg1] << " = " << regs[curr_cpu][arg1] << " (CPU " << curr_cpu+1 << ")"  << "\n";
        pc[curr_cpu]++;
        tot_instructions++;
    }
}

bool safe_register(int reg, int curr_cpu) {
    if(!jobs.empty() && jobs.front().reg == reg && jobs.front().cpu == curr_cpu) return false;
    if(requests[curr_cpu][reg].size() != 0) return false;
    return true;
}

bool safe_register1(int reg, int curr_cpu) {
    for(auto it = all_requests.begin(); it != all_requests.end(); it++) { // here we should also check if it is in job 
        if(it->cpu == curr_cpu && it->reg == reg && it->type == "lw") return false;
    }
    return true;
}

// Checks if an instruction is safe for execution
bool is_safe(int index, int curr_cpu) {
    if(!OPTIMIZE) {
        for(int i = 0; i < 32; i++) if(!safe_register(i, curr_cpu)) return false;
        return true;
    }

    string func = mp[curr_cpu][index].func;
    int arg1 = mp[curr_cpu][index].arg1;
    int arg2 = mp[curr_cpu][index].arg2;
    int arg3 = mp[curr_cpu][index].arg3;

    if(func == "add" || func == "sub" || func == "mul" || func == "slt" || func == "addi") {
        if(dram_writing_flag) return false;
    }

    if (func == "lw"){
        if(!requests[curr_cpu][arg1].empty()){
            request pre = requests[curr_cpu][arg1].back();
            if(pre.type == "lw") {
                return true;
            }
        }
        if(all_requests.size() >= MAX_MRM_SIZE) return false;
        else return true;
    }

    if (func=="sw"){
        int loc = arg2 + regs[curr_cpu][arg3];
        int max_clock_cycle = -1;
        for(request req : all_requests) {
            if(req.loc == loc) max_clock_cycle = max(max_clock_cycle, req.req_cycle);
        }
        for(auto it = all_requests.begin(); it != all_requests.end(); ++it) { 
            if(it->req_cycle == max_clock_cycle && it->type == "sw") {
                return true;
            }
        }
        if(all_requests.size() >= MAX_MRM_SIZE) return false;
        else return true;            
    }
    
    if(func == "j") return true;
    if(!safe_register(arg1, curr_cpu)){
        unsafe_reg[curr_cpu] = arg1;
        return false;
    }
    if(!safe_register1(arg2, curr_cpu)) {
        unsafe_reg[curr_cpu] = arg2;
        return false;
    }
    if(func == "addi" || func == "bne" || func == "beq") return true;
    else {
        if (!safe_register1(arg3, curr_cpu)){
            unsafe_reg[curr_cpu] = arg3;
            return false;
        }
        return true;
    }
}

// Run the program
void run_program() {
    while(tot_cycles < m) {
        if(dram_writing_flag) dram_writing_flag = false;
        if(buffer_bottleneck) buffer_bottleneck = false;
        tot_cycles++;
        count_loading_cycles++;
        if(VERBROSE) cout << "Cycle: " << tot_cycles << "\n";
        execute_job();
        for(int i = 0; i < cpus; i++) {
            if(!valid[i]) continue;
            cycles[i]++;
            if(pc[i] != line[i] && is_safe(pc[i], i) && !buffer_bottleneck) execute_ins(pc[i], i);
            if(VERBROSE) {
                cout << "Register values for CPU: " << i+1 << "\n";
                for(int j = 0; j < 32; j++) {
                    cout << regs[i][j] << " ";
                }
                cout << "\n" << "\n";
            }
        }
    }
}

// Process each instruction (mainly check for syntax errors)
void process_instruction(vector<string> tokens , int curr_cpu) {
    regex r("\\$r([0-9]|1[0-9]|2[0-9]|3[0-1])");
    regex r1("([0-9]*|-[0-9]*)\\(\\$r([0-9]|1[0-9]|2[0-9]|3[0-1])\\)");
    regex r2("[0-9]*|-[0-9]*");

    int size = tokens.size();
    if(size < 2 || size > 4) {
        cout << "Number of tokens in the instructions must be between two and four on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
        valid[curr_cpu] = false;
        return;
    }

    string func = tokens[0];
    int arg1 = -1, arg2 = -1, arg3 = -1, count = 0;
    string tag = "";
    if(func == "add") {
        if(size != 4) {
            cout << "Three arguments must be provided to add instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end() && name_regs.find(tokens[3]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            arg3 = name_regs[tokens[3]];
        } else {
            cout << "All arguments to add must be registers on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
    } else if(func == "sub") {
        if(size != 4) {
            cout << "Three arguments must be provided to sub instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end() && name_regs.find(tokens[3]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            arg3 = name_regs[tokens[3]];
        } else {
            cout << "All arguments to sub must be registers on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
    } else if(func == "mul") {
        if(size != 4) {
            cout << "Three arguments must be provided to mul instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end() && name_regs.find(tokens[3])!=name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            arg3 = name_regs[tokens[3]];
        } else {
            cout << "All arguments to mul must be registers on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
    } else if(func == "beq") {
        if(size != 4) {
            cout << "Three arguments must be provided to beq instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            tag = tokens[3];
        } else {
            cout << "First two arguments to beq must be registers on line and third must be memory location on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
    } else if(func == "bne") {
        if(size != 4) {
            cout << "Three arguments must be provided to bne instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            tag = tokens[3];
        } else {
            cout << "First two arguments to bne must be registers on line and third must be memory location on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
    } else if(func == "slt") {
        if(size != 4) {
            cout << "Three arguments must be provided to slt instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1]) != name_regs.end() && name_regs.find(tokens[2]) != name_regs.end() && name_regs.find(tokens[3]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            arg3 = name_regs[tokens[3]];
        } else {
            cout << "All arguments to slt must be registers on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }        
    } else if(func == "j") {
        if(size != 2) {
            cout << "One argument must be provided to j instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        tag = tokens[1];

    } else if(func == "lw") {
        if(size != 3) {
            cout << "Two arguments must be provided to lw instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            int op = tokens[2].find("(");
            if(op != 0) {
                if (!regex_match(tokens[2].substr(0,op), r2)){
                    cout << "First argument must be register and second must be memory on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
                    valid[curr_cpu] = false;
                    return;
                }
                try{
                    arg2 = stoi(tokens[2].substr(0,op));
                }
                catch(std::out_of_range& e){
                    cout << "Value is out of range on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
                    valid[curr_cpu] = false;
                    return;
                }
            } else arg2 = 0;
            if (name_regs.find(tokens[2].substr(op+1, tokens[2].size()-op-2)) == name_regs.end()){
                cout << "First argument must be register and second must be memory on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
                valid[curr_cpu] = false;
                return;
            }
            arg3 = name_regs[tokens[2].substr(op+1, tokens[2].size()-op-2)];
        } else {
            cout << "First argument must be register and second must be memory on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
    } else if(func == "sw") {
        if(size != 3) {
            cout << "Two arguments must be provided to sw instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1]) != name_regs.end()) {
            arg1 = name_regs[tokens[1]];
            int op = tokens[2].find("(");
            if(op != 0) {
                if (!regex_match(tokens[2].substr(0,op), r2)) {
                    cout << "First argument must be register and second must be memory on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
                    valid[curr_cpu] = false;
                    return;
                }
                try{
                    arg2 = stoi(tokens[2].substr(0,op));
                }
                catch(std::out_of_range& e){
                    cout << "Value is out of range on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
                    valid[curr_cpu] = false;
                    return;
                }
            } else arg2 = 0;
            if (name_regs.find(tokens[2].substr(op+1, tokens[2].size()-op-2)) == name_regs.end()){
                cout << "First argument must be register and second must be memory on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
                valid[curr_cpu] = false;
                return;
            }
            arg3 = name_regs[tokens[2].substr(op+1, tokens[2].size()-op-2)];
        } else {
            cout << "First argument must be register and second must be memory on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }       
    } else if(func == "addi") {
        if(size != 4) {
            cout << "Three arguments must be provided to addi instruction on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }
        if(name_regs.find(tokens[1])!=name_regs.end() && name_regs.find(tokens[2])!=name_regs.end() && regex_match(tokens[3], r2)) {
            arg1 = name_regs[tokens[1]];
            arg2 = name_regs[tokens[2]];
            try{
                arg3 = stoi(tokens[3]);
            }
            catch(std::out_of_range& e){
                cout << "Value is out of range on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
                valid[curr_cpu] = false;
                return;
            }
        } else {
            cout << "First two arguments to add must be registers on line and last must be integer on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
            valid[curr_cpu] = false;
            return;
        }        
    } else {
        cout << "Unrecognised function: " + func + " on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << " of CPU " << curr_cpu+1 << "\n";
        valid[curr_cpu] = false;
        return;
    }

    instruction ins = {func, tag, arg1, arg2, arg3, count};
    mp[curr_cpu][line[curr_cpu]] = ins;
}

// Read and tokenize the contents of input file
void read_file(string file_name , int curr_cpu) {
    ifstream file(file_name);
    string ins;
    while(getline(file, ins)) {
        if(!valid[curr_cpu]) return;
        vector<string> tokens;
        vector<string> tokens_temp;
        ins = trim(ins);
        boost::split(tokens_temp, ins, boost::is_any_of(", "));
        for(string token : tokens_temp) if(token != "") tokens.push_back(token);
        line_to_number[curr_cpu][line[curr_cpu]] = line_number[curr_cpu];
        if(tokens.size() == 1) {
            string func = tokens[0];
            int op = func.find(":");
            if (op != -1) {
                string tag = func.substr(0,op);
                tags[curr_cpu][tag] = line[curr_cpu];
            } else {
                cout << "Unrecognised function: " + func + " on line " << line_to_number[curr_cpu][line[curr_cpu]]+1 << "\n";
                valid[curr_cpu] = false;
                return;
            }
        } else if(tokens.size() > 1) {
            process_instruction(tokens, curr_cpu);
            line[curr_cpu]++;
            tot_lines++;
        }
        line_number[curr_cpu]++;
    }
}

void read_all_files(string folder) {
    for(int i = 0; i < cpus; i++) {
        string file_name = folder+"f" + to_string(i+1);
        read_file(file_name, i);
    }
}

// Initialise various global variables
void initialise() {
    for(int i = 0; i < cpus; i++) {
        pc[i] = 0;
        line[i] = 0;
        line_number[i] = 0;
        cycles[i] = 0;
        unsafe_reg[i] = -1;
        mp[i].clear();
        valid[i] = true;
        offset[i] = i*block_size;
        for(int j = 0; j < (1<<5); j++) regs[i][j] = 0;
    }

    dram_writing_flag = false;
    dram_loading = false;
    count_loading_cycles = REQUEST_LOADING_DELAY;

    buffer_bottleneck = false;

    tot_cycles = 0;
    tot_lines = 0;
    tot_instructions = 0;

    curr_cmd = "";
    curr_end = -1;
    
    buff_row = -1;
    row_read = 0, row_write = 0, col_read = 0, col_write = 0;
    for(int i = 0; i < (1<<18); i++) mem[i] = 0;
    name_regs = {{"$zero", 0}, {"$at", 1}, {"$v0", 2}, {"$v1", 3}, {"$a0", 4}, {"$a1", 5}, {"$a2", 6}, {"$a3", 7}, {"$t0", 8}, {"$t1", 9}, {"$t2", 10}, {"$t3", 11}, {"$t4", 12}, {"$t5", 13}, {"$t6", 14}, {"$t7", 15}, {"$s0", 16}, {"$s1", 17}, {"$s2", 18}, {"$s3", 19}, {"$s4", 20}, {"$s5", 21}, {"$s6", 22}, {"$s7", 23}, {"$t8", 24}, {"$t9", 25}, {"$k0", 26}, {"$k1", 27}, {"$gp", 28}, {"$sp", 29}, {"$fp", 30}, {"$ra", 31}};
    regs_name = {{0, "$zero"}, {1, "$at"}, {2, "$v0"}, {3, "$v1"}, {4, "$a0"}, {5, "$a1"}, {6, "$a2"}, {7, "$a3"}, {8, "$t0"}, {9, "$t1"}, {10, "$t2"}, {11, "$t3"}, {12, "$t4"}, {13, "$t5"}, {14, "$t6"}, {15, "$t7"}, {16, "$s0"}, {17, "$s1"}, {18, "$s2"}, {19, "$s3"}, {20, "$s4"}, {21, "$s5"}, {22, "$s6"}, {23, "$s7"}, {24, "$t8"}, {25, "$t9"}, {26, "$k0"}, {27, "$k1"}, {28, "$gp"}, {29, "$sp"}, {30, "$fp"}, {31, "$ra"}};
    
}

int main(int argc, char* argv[]) {
    if(argc != 4 && argc != 6) {
        cout << "Please execute the program as ./program <folder_name> N M ROW_ACCESS_DELAY COLUMN_ACCESS_DELAY" << "\n";
        exit(1);
    }
    string folder = argv[1];
    cpus = stoi(argv[2]);
    m = stoi(argv[3]);
    if(argc == 6) {
        ROW_ACCESS_DELAY = stoi(argv[4]);
        COL_ACCESS_DELAY = stoi(argv[5]);
    }

    block_size = (1 << 20) / cpus;
    while(block_size%4 != 0) block_size++;

    initialise();
    read_all_files(folder);
    run_program();
    print_stats();
}