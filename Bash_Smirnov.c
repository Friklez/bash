#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

//одна из команд текущего ввода; разделены по &

struct Input {
    char* error;
    char*** commands;   //команды конвейера
    bool background;   //включен ли фоновый режим для данной команды
    char * inputFile;  //входной поток
    char * outputFile;  //выходной поток
    bool clear_output;  //дописать в файл или затереть его
    int mem;      //кол-во единиц sizeof, выделенных на данный момент для commands
    int size;      //текущий размер commands
};

//введенное выражение, раздробленное на команды относительно фонового режима/контейнера

struct Global_Input {
    bool is_eof;    //конец ли ввода
    int size;
    struct Input* data;  //вектор команд, разделенных по &
    int mem;

};

struct String {
    int size;
    char* start;
    int mem;
};



//конкретная команда (массив строк)

struct Command {
    int size;
    char** start;
    int mem;
    bool * is_special_command;
};


void ZombieKiller() {

    int wait_result = waitpid(-1, NULL, WNOHANG);
    while (wait_result > 0) {
       // printf("Killing process %d\n",wait_result);
        fflush(stdout);
        wait_result = waitpid(-1, NULL, WNOHANG);
    }
}
//печать приглашения к вводу

void PrintInvitation() {

    int MAXDIR = 100;
    char dir[MAXDIR];
    getcwd(dir, MAXDIR);
    printf("%s ~ $ ", dir);
    fflush(stdout);
}

//сравнение строк

bool Str_Equals(const char* lhs, const char* rhs) {
    while ((*lhs != '\0') && (*rhs != '\0') && (*lhs == *rhs)){
        lhs ++;
        rhs ++;
    }
    return (*lhs == *rhs);
}

//печать ошибки

void PrintError(struct Input* input) {
    if (input->error == NULL) {
        return;
    }

    printf("Error: %s\n", input->error);
    fflush(stdout);
}


//перенаправление ввода/вывода

void StreamUpdateInput(struct Input* input, bool * er) {
    int fd_input;

    if (input->inputFile != NULL) {
        fd_input=open(input->inputFile, O_RDONLY);
        if (fd_input < 0) {
            input->error = "failed to open file";
            *er = true;
            return;
        }
        if ( dup2(fd_input, 0) == -1) {
            input->error = "failed to dup2 fd_input";
            *er = true;
            return;
        }
        close(fd_input);
    }
}

void StreamUpdateOutput(struct Input* input, bool * er) {
    int fd_output;
    if (input->outputFile != NULL) {
        if (input->clear_output) {
            fd_output = open(input->outputFile, O_CREAT | O_TRUNC | O_WRONLY, 0666);
            if (fd_output < 0) {
                input->error = "failed to open file";
                *er = true;
                return;
            }

            if (dup2(fd_output, 1) == -1) {
                input->error = "failed to dup2 fd_output";
                *er = true;
                return;
            }

            close(fd_output);
        } else {
            fd_output = open(input->outputFile, O_CREAT | O_WRONLY | O_APPEND, 0666);
            if (fd_output < 0) {
                input->error = "failed to open file";
                *er = true;
                return;
            }


            if (dup2(fd_output, 1) == -1) {
                input->error = "failed to dup2 fd_output";
                *er = true;
                return;
            }

            close(fd_output);
        }

    }
}


void ClosePipes(int* pipes, int pipes_size) {
    for (int i = 0; i < pipes_size; i++) {
        close(pipes[i]);
    }
}


void CommandRun(struct Input* input, bool* eof, int child_2, bool background) {
    const int pipes_size = (input->size - 1) * 2;
    int* pipes = (int*)malloc(pipes_size * sizeof(int));

    for (int i = 0; i < pipes_size; i += 2) {
        if (pipe(pipes + i) == -1) {
            input->error = "failed to make a pipe!";
            return;
        }
    }

    int* childs = (int*)malloc(input->size* sizeof(int));


    for (int i = 0; i < input->size; i++) {
        int child = fork();
        if (child == -1) {
            input->error ="failed to fork!\n";
            return;
        }

        if (child == 0) {

            int prev_istream = dup(0);
            if (prev_istream ==-1) {
                input->error = "failed to dup STDIN";
                return;
            }
            int prev_ostream = dup(1);
            if (prev_ostream == -1) {
                input->error = "failed to dup STDOUT";
                return;
            }

            if (i == 0) {
                bool er = false;
                StreamUpdateInput(input,&er);
                if (er) {
                    return;
                }
            }

            if (i > 0) {
                if (dup2(pipes[(i - 1) * 2], 0) == -1) {
                    input->error = "failed to dup2!\n";
                    return;
                }
            }
            if (i + 1 < input->size) {
                if (dup2(pipes[i * 2 + 1], 1) == -1) {
                    input->error = "failed to dup2!\n";
                    return;
                }
            }
            if (i == input->size - 1) {
                bool er = false;
                StreamUpdateOutput(input,&er);
                if (er) {
                    return;
                }
            }


            ClosePipes(pipes, pipes_size);
            free(pipes);

            execvp(input->commands[i][0], input->commands[i]);
            if (dup2(prev_istream, 0) == -1) {
                input->error = "failed to dup2 prev_istream\n";
                return;
            }


            if (dup2(prev_ostream, 1) == -1) {
                input->error = "failed to dup2 prev_ostream\n";
                return;
            }
            if (!background) {
                input->error = "failed to run command";
            } else {
                input->error = "Error while background mode: failed to run command";
            }

            *eof = true;
            return;

        }


        childs[i] = child;
    }


    ClosePipes(pipes, pipes_size);
    free(pipes);

    for (int i = 0; i < input->size; i++) {
        if (waitpid(childs[i], NULL, 0) == -1) {
            printf("conveyor error: failed to wait child process #%d with pid %d\n", i, childs[i]);
            fflush(stdout);
        }
    }
    free(childs);
    if (background) {
        printf("\nBackground process %d has finished\n", child_2);
        fflush(stdout);
        exit(0);
    }
}


//КОМАНДЫ ДЛЯ РАБОТЫ С ДИНАМИЧЕСКОЙ СТРОКОЙ

//инициализация

void Str_Init(struct String* s) {
    assert(s != NULL);
    s->size = 1;
    s->mem = 16;
    s->start = (char*) malloc(s->mem *sizeof(char));
    (s->start)[0] = '\0';
}

//получение

char* Str_Get(const struct String* s) {
    char* res = (char*)malloc((s->size)*sizeof(char));
    for (int i = 0; i < s->size; i++) {
        res[i] = s->start[i];
    }

    return res;
}

//освобождение памяти

void Str_Free(struct String* s) {
    s->size = 0;
    s->mem = 0;
    free(s->start);
    s->start = NULL;
}

//расширение

void Str_Extend(struct String* str, const char a) {
    if (str->mem < (str->size + 1)) {
        str->mem *= 2;
        char* temp = (char*)malloc(str->mem*sizeof(char));
        for (int i = 0; i < str->size - 1; i++) {
            temp[i] = str->start[i];
        }
        temp[str->size - 1] = a;
        temp[str->size] = '\0';
        free(str->start);
        str->start = temp;
    } else {
        str->start[str->size - 1] = a;
        str->start[str->size] = '\0';
    }
    str->size ++;
}

// считывание

char* Str_Read(int* temp) {
    if (*temp == ' ' || *temp == '\n' || *temp == EOF) {
        return NULL;
    }
    int SINGLE_QUOTE = 0; //состояние автомата, соответствующее ''
    int DOUBLE_QUOTE = 0; //состояние автомата, соответствующее ""
    struct String s;
    Str_Init(&s);
    while (SINGLE_QUOTE || DOUBLE_QUOTE || (*temp != ' ' && *temp != '\n' && *temp != EOF)) {

        if (SINGLE_QUOTE) {
            if (*temp == '\'') {
                SINGLE_QUOTE = 0; //выход
            } else {
                Str_Extend(&s, *temp);
            }
            *temp = getchar();
        } else if (DOUBLE_QUOTE) {
            if (*temp == '"') {
                DOUBLE_QUOTE = 0; //выход
            } else {
                if (*temp == '\n') {
                    Str_Extend(&s, *temp);
                    *temp = getchar();
                    continue;
                }
                if (*temp == '\\') {           //находим экранирование
                    *temp = getchar();
                    if (*temp == EOF) {
                        continue;
                    }
                    if (*temp == '\n') {
                        *temp = getchar();
                        continue;
                    }
                }
                Str_Extend(&s, *temp);
            }
            *temp = getchar();
        } else {
            if (*temp == '&' || *temp == '|' || *temp == '>' || *temp == '<') {
                break;
            }
            if (*temp == '\'') {
                SINGLE_QUOTE = 1;
                *temp = getchar();
                continue;
            }
            if (*temp == '"') {
                DOUBLE_QUOTE = 1;
                *temp = getchar();
                continue;
            }
            if (*temp == '\\') {           //находим экранирование
                *temp = getchar();
                if (*temp == EOF) {
                    continue;
                }
                if (*temp == '\n') {        //если экранирование перевода строки,игнорируем. иначе: это важный для нас символ
                    *temp = getchar();
                    continue;
                }
            }
            Str_Extend(&s, *temp);
            *temp = getchar();
        }
    }
    char* res = Str_Get(&s);
    Str_Free(&s);

    return res;
}

//КОМАНДЫ ДЛЯ РАБОТЫ С КОМАНДОЙ (МАССИВОМ СТРОК)

//инициализация

void Command_Init(struct Command* ar) {
    assert(ar != NULL);
    ar->size = 0;
    ar->mem = 16;
    ar->start = (char**) malloc(ar->mem *sizeof(char *));
    ar->is_special_command = (bool *) malloc(ar->mem*sizeof(bool));
}


//расширение

void Command_Extend(struct Command* ar, char* a, bool special_flag) {

    if (ar->mem < (ar->size + 1)) {
        ar->mem *= 2;
        char** temp = (char**)malloc(ar->mem*sizeof(char *));
        bool* temp_bool = (bool *) malloc(ar->mem*sizeof(bool));
        for (int i = 0; i < ar->size; i++) {
            temp[i] = ar->start[i];
            temp_bool[i] = ar->is_special_command[i];

        }

        temp[ar->size] = a;
        temp_bool[ar->size] = special_flag;
        for (int q = 0;q < ar->size; q++) {
            free(ar->start[q]);
        }
        free(ar->start);
        free(ar->is_special_command);
        ar->start = temp;
        ar->is_special_command = temp_bool;

    } else {
        ar->start[ar->size] = a;
        ar->is_special_command[ar->size] = special_flag;
    }
    ar->size ++;
}

//построение команды

struct Command Command_Read(bool* eof) {
    struct Command ar;
    Command_Init(&ar);               //инициализируем структуру для хранения лексем
    int symb = getchar();
    while(symb != '\n' && symb != EOF) {
        if (symb == '&' || symb == '|' || symb == '>' || symb == '<') {
            struct String s;
            Str_Init(&s);
            Str_Extend(&s, symb);
            const char oldsymb = symb;
            symb = getchar();
            if (oldsymb == '>' && symb == '>') {
                Str_Extend(&s, symb);
                symb = getchar();
            }
            char* res = Str_Get(&s);
            Str_Free(&s);
            Command_Extend(&ar, res, true);
        }
        else {
            char* ar_el = Str_Read(&symb);   //вводим очерендую лексему

            if (ar_el != NULL) {
                Command_Extend(&ar, ar_el, false);
            }
        }
        while (symb==' ') symb = getchar();//пропускаем все пробелы
    }

    *eof = symb == EOF;

    ar.start[ar.size] = NULL;
    ar.is_special_command[ar.size] = false;
    return ar;
}


// КОМАНДЫ ДЛЯ РАБОТЫ С INPUT

//инициализация

void Input_Init(struct Input* input) {
    input->error = NULL;

    input->mem = 3;
    input->size = 0;
    input->commands = (char***) malloc(input->mem *sizeof(char **));

}

void Input_Params(struct Input* input, bool background_flag, char * inputF, char * outputF, bool clear_out) {
    input->background = background_flag;
    input->inputFile = inputF;

    input->outputFile = outputF;

    input->clear_output = clear_out;
}
//освобождение памяти

void Free_Input(struct Input* input) {

    for (int i = 0; i < input->size; i++) {

        char** word_ptr = input->commands[i];
        while (*word_ptr != NULL) {
            free(*word_ptr);
            ++word_ptr;
        }
        free(input->commands[i]);
    }
    free(input->commands);
    if (input->inputFile != NULL) {
        free(input->inputFile);
    }
    if (input->outputFile != NULL) {
            free(input->outputFile);
        }

}

//расширение

void Input_Extender(struct Input* input, char** temp_command) {
    if (input->mem <= (input->size + 1)) {
        input->mem *= 2;
        char*** temp = (char***)malloc(input->mem * sizeof (char**));
        for (int i = 0; i < input->size; i++) {
            temp[i] = input->commands[i];
        }

        temp[input->size] = temp_command;
        Free_Input(input);
        input->commands = temp;

    } else {
        input->commands[input->size] = temp_command;
    }
    input->size ++;
}


// КОМАНДЫ ДЛЯ РАБОТЫ С GLOBAL_INPUT

//инициализация

void Global_Input_Init(struct Global_Input* global_input) {
    global_input->is_eof = false;
    global_input->size = 0;
    global_input->mem = 4;
    global_input->data = (struct Input*) malloc(global_input->mem *sizeof(struct Input));
}

//освобождение памяти

void Free_Global_Input(struct Global_Input* global_input) {
    for (int i = 0; i < global_input->size; i++) {
        Free_Input(&global_input->data[i]);
    }
    free(global_input->data);
}

//расширение

void Global_Input_Extend(struct Global_Input* global_input, struct Input* input) {

    if (global_input->mem <= (global_input->size + 1)) {
        global_input->mem *= 2;
        struct Input* temp = (struct Input*)malloc(global_input->mem * sizeof (struct Input));
        for (int i = 0; i < global_input->size; i++) {
            temp[i] = global_input->data[i];
        }

        temp[global_input->size] = *input;
        Free_Global_Input(global_input);
        global_input->data = temp;

    } else {
        global_input->data[global_input->size] = *input;
    }
    global_input->size ++;
}



struct Input Read_Input_Support(bool background, char ** * left_border,
        char **  *current_pos, struct Command command, bool * inp_error) {
    struct Input current_input;
    Input_Init(&current_input);

    char * inputF = NULL;
    char * outputF = NULL;
    char * error_str = NULL;

    bool clear_out = false;

    while(*left_border != *current_pos) {
        struct Command temp;
        Command_Init(&temp);
        while (*left_border != *current_pos && (!Str_Equals(**left_border,"|") || !command.is_special_command[*left_border-command.start])) {

            if (Str_Equals(**left_border,">") && command.is_special_command[*left_border-command.start]) {
                free(**left_border);
                (*left_border) ++;
                if (!command.is_special_command[*left_border - command.start]) {
                    outputF = **left_border;
                    clear_out = true;
                } else {
                    *inp_error = true;
                    error_str = "unexpected special symbol after >";
                    (*left_border) --;
                }
            } else {
                if (Str_Equals(**left_border, ">>") && command.is_special_command[*left_border - command.start]) {
                    free(**left_border);
                    (*left_border) ++;
                    if (!command.is_special_command[*left_border - command.start]) {
                        outputF = **left_border;
                        clear_out = false;
                    } else {
                        *inp_error = true;
                        error_str = "unexpected special symbol after >>";
                        (*left_border) --;
                    }
                } else {
                    if (Str_Equals(**left_border, "<") && command.is_special_command[*left_border - command.start]) {
                        free(**left_border);
                        (*left_border) ++;
                        if (!command.is_special_command[*left_border - command.start]) {
                            inputF = **left_border;
                            clear_out = true;
                        } else {
                            *inp_error = true;
                            error_str = "unexpected special symbol after <";
                            (*left_border) --;
                        }

                    } else {
                        Command_Extend(&temp, **left_border, false);
                    }
                }
            }

            (*left_border) ++;
        }
        if (*left_border != *current_pos) {
            free(**left_border);
            (*left_border) ++;
        }
        Command_Extend(&temp, NULL, false);
        Input_Extender(&current_input, temp.start);
        free(temp.is_special_command);
    }
    bool background_flag = background;
    Input_Params(&current_input, background_flag, inputF, outputF, clear_out);
    if (error_str != NULL) {
        current_input.error = error_str;
    }
    return current_input;

}

void ReadInput(struct Global_Input* global_input, bool * inp_error) {
    Global_Input_Init(global_input);
    bool eof = false;
    struct Command command = Command_Read(&eof); //считываем всю введенную строку


    char **current_pos = command.start; //указатель, который мы будем двигать вправо и находить &,
    //чтобы произвести дробление Global_Input на Input

    char** left_border = current_pos; //как только нашли &, заносим в текущий input
    // массив строк со строки *left по *current_pos

    bool amper = false;

    while (*current_pos != NULL) {
        if (Str_Equals(*current_pos, "&") && (command.is_special_command[current_pos - command.start])) {
            struct Input current_input = Read_Input_Support(true, &left_border,
                    &current_pos, command, inp_error);
            Global_Input_Extend(global_input, &current_input);
            left_border ++;
            amper = true;
            free(*current_pos);
            current_pos ++;
            continue;

        }
        amper = false;
        current_pos ++;
    }

    if (current_pos != command.start) {
        current_pos --;
    }

    //если последняя команда большой строки не должна выполняться в фоновом режиме, то обработаем ее отдельно

    if (*current_pos != NULL && !amper) {

        current_pos ++;
        struct Input current_input = Read_Input_Support(false, &left_border,
                &current_pos, command, inp_error);

        Global_Input_Extend(global_input, &current_input);

    }
    free(command.is_special_command);
    free(command.start);
    global_input->is_eof = eof;
}


bool RunInputEmbedded(struct Input* input, bool* eof) {

    if (input->size > 1) {
        for (int j = 0; j < input->size; j++) {
            if (Str_Equals(input->commands[j][0], "cd") || Str_Equals(input->commands[j][0], "exit")) {
                input->error = "Conveyor doesn't support commands such as exit or cd";
                return true;
            }
        }

    }

    if (Str_Equals(input->commands[0][0], "exit")) {
        if (input->commands[0][1] == NULL) {
            *eof = true;
        } else {
            input->error = "exit does not expect arguments";
        }
        return true;
    }

    if (Str_Equals(input->commands[0][0], "cd")) {
        const bool empty = input->commands[0][1] == NULL;

        if (!empty && input->commands[0][2] != NULL) {
            input->error = "cd expects less than two arguments";
        } else {

            if (input->background) {

                int mini_child = fork();
                if (mini_child == -1) {
                    input->error = "failed to fork proccess";
                    return true;
                }
                if (mini_child == 0) {


                    if (empty) {

                        if (chdir("..") == -1) {
                            input->error = "failed to change current working dir";
                            *eof = true;


                        } else {
                            printf("\nBackground process %d has finished\n", getpid());
                            fflush(stdout);

                        }
                        exit(0);

                    } else {

                        if (chdir(input->commands[0][1]) == -1) {
                            input->error = "failed to change current working dir";
                            *eof = true;


                        } else {
                            printf("\nBackground process %d has finished\n", getpid());
                            fflush(stdout);

                        }
                        exit(0);
                    }
                }

                else {

                }
            } else {

                if (empty) {
                    char * tempir = malloc(2 * sizeof(char));
                    tempir[0] = '.';
                    tempir[1] = '.';
                    if (chdir(tempir) == -1) {
                        input->error = "failed to change current working dir";
                    }
                    free(tempir);

                } else {
                    if (chdir(input->commands[0][1]) == -1) {
                        input->error = "failed to change current working dir";
                    }
                }

            }

        }
        return true;
    }

    return false;
}


void RunInput(struct Input* input, bool* eof) {

    if (input->error != NULL) {
        return;
    }
    
    if (input->commands[0][0] == NULL) {
        return;
    }

    *eof = false;

    if (RunInputEmbedded(input, eof)) {

        return;
    }

    if (input->background) {

        int child_1 = fork();
        if (child_1 == -1) {
            input->error = "failed to fork proccess";
            return;
        }

        if (child_1 == 0) {

            CommandRun(input, eof, getpid(), true);

            PrintError(input);
        } else {
        }
    }
    else {
        CommandRun(input, eof, 0, false);

    }
}

//процедура обработки текущей команды (еще не разделенной на логические части по & и |)
void Processing(struct Global_Input* global_input) {

    bool eof = global_input->is_eof;

    for (int i = 0; i < global_input->size; i++) {

        RunInput(&global_input->data[i], &eof);
        PrintError(&global_input->data[i]);
    }

    Free_Global_Input(global_input);
    global_input->is_eof = eof;

}

int main() {
    struct Global_Input global_input;

    do {

        bool inp_error = false;
        PrintInvitation();
        ReadInput(&global_input, &inp_error);
        if (inp_error) {
            for (int i = 0; i < global_input.size; i++) {

                PrintError(&global_input.data[i]);
            }
            continue;
        }
        Processing(&global_input);
        ZombieKiller();

    } while (!global_input.is_eof);

    return 0;
}

