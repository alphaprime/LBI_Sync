#include <time.h>
#include <sys/time.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <stdlib.h>
#include <thread>
#include <math.h>
#include <atomic>
#include <string.h>
#include <mutex>
using namespace std;
int max_iter, n_thread,num_workers,sample_number = 0, max_n = 0, m =0;
double p,kappa,alpha,Top;
double*s, *z, *Gamma, *update, *add_time;
vector<int> Row, Col, Weight;

struct Range {
  int start;  // starting index
  int end;    // pass the end index

  Range(int s, int e) : start(s), end(e) {}
};

class Barrier{
 public:
  void wait();
  Barrier(const Barrier&)= delete; //dangerous, so disallow
  Barrier(int thread_count=0);
  void init(int nthreads){thread_count = nthreads;}
 private:
  std::atomic<int> counter[2];
  int lock[2];
  int cur_idx;
  int thread_count;
};

Barrier::Barrier(int thread_count):thread_count(thread_count){
    counter[0].store(0);counter[1].store(0);
    lock[0]=lock[1]=cur_idx=0;
}

void Barrier::wait(){
  //int idx= cur_idx.load();
  int idx = cur_idx;
  //if the current lock is not locked, lock it
  //if(lock[idx].load() == 0){
  if(lock[idx] == 0){
    //lock[idx].store(1);
    lock[idx] = 1;
  }
  //val is equal to the value of the counter before increment
  int val = counter[idx].fetch_add(1);
  
  //if wait has been called at least thread_count many times
  if( val >= thread_count-1){
    counter[idx].store(0); //counter for current idx has reached its max
    lock[idx]=0;
    cur_idx = cur_idx ^ 1;
    //cur_idx.fetch_xor(1); //replace 0 with 1 and 1 with 0
    //lock[idx].store(0); //deactive lock associated with the old active idx
  }
  while(lock[idx] == 1);
//while(lock[idx].load() == 1 ); //spin while waiting for other threads to hit barrier
}

void sync_worker(int rank, Barrier& Computation_Barrier, Barrier& Cache_Update_Barrier);

double get_wall_time(){
  struct timeval time;
  if (gettimeofday(&time,NULL)){
    //  Handle error
    return 0;
  }
  return (double)time.tv_sec + (double)time.tv_usec * .000001;
}
double get_cpu_time(){
  return (double)clock() / CLOCKS_PER_SEC;
}

int main(int argc, char *argv[]) {
  char* input_file, *output_file;
  if (argc<2){
    cout << "Error" << endl;
    exit(-1);
  }else{
    input_file = argv[1];
    if (argc<3){
      output_file = "output_file";
    }else {
      output_file = argv[2];
      if (argc<4){
        kappa = 10.0;
      }else{
        kappa = atof(argv[3]);
        if (argc<5){
          alpha = 1.0; 
        }else{
          alpha = atof(argv[4]);
          if (argc<6){
            n_thread = 4;
          }else{
            n_thread = atoi(argv[5]);
            if (argc<7){
              max_iter = 10000;
            }else{
              max_iter = atoi(argv[6]);
            }
          }
        }
      }
    }
  }

  vector<int> a, b;
  ifstream infile;
  infile.open(input_file);
  char buffer[256];
  if (! infile.is_open())
  { cout << "Error opening file"; exit (1);}
  while (!infile.eof())
  {
    infile.getline(buffer,256);
    int ind1,ind2;
    sscanf(buffer,"%i %i",&ind1,&ind2);
    a.push_back(ind1);
    b.push_back(ind2);
    if (ind1 > max_n)
      max_n = ind1;
    if (ind2 > max_n)
      max_n = ind2;
    /*int index = atoi(strtok(buffer," "));
    cout << index <<endl;
    a.push_back(index);
    if (index > max_n)
      max_n = index;
    cout << buffer <<endl;
    index = atoi(strtok(NULL, " \t\n"));
    cout << index <<endl;
    b.push_back(index);
    if (index > max_n)
      max_n = index;*/
    sample_number ++;
  }
  infile.close();
  int** temp = new int*[max_n];
  for (size_t i =0; i<max_n; i++){
    temp[i] = new int[max_n]{0};
    memset(temp[i], 0, sizeof(int) * max_n);
  }
  for (size_t i =0; i<sample_number; i++){
    temp[a[i]-1][b[i]-1]++;
  }

  for (int i =0; i<max_n; i++){
    for(int j =0; j<max_n; j++){
      if(temp[i][j] != 0){
        Row.push_back(i);
        Col.push_back(j);
        Weight.push_back(temp[i][j]);
        m++;
      }
    }
  }

  Top = p*sample_number;
  num_workers = n_thread;
  s = new double[max_n];
  memset(s, 0.0, sizeof(double) * max_n);
  Gamma = new double[m];
  memset(Gamma, 0.0, sizeof(double) * m);
  z = new double[m];
  memset(z, 0.0, sizeof(double) * m);
  add_time = new double[m];
  memset(add_time, 0.0, sizeof(double) * m);
  update = new double[num_workers*max_n];
  memset(update, 0.0, sizeof(double) * n_thread*max_n);
  double start_time = get_wall_time();
  

  std::vector<std::thread> mythreads;
  Barrier computation_barrier(num_workers);
  Barrier cache_update_barrier(num_workers);

  for (size_t i = 0; i < num_workers; i++) {
      // partition the indexes
      mythreads.push_back(std::thread(sync_worker,i,std::ref(computation_barrier),std::ref(cache_update_barrier)));
  }
  for (size_t i = 0; i < num_workers; i++) {
    mythreads[i].join();
  }
  double end_time = get_wall_time();
  cout << "Compeleted. Computation Time: " << end_time - start_time << endl;
  ofstream out(output_file);
  if (out.is_open())
  {
    for(size_t i=0;i<m;i++){
      out << add_time[i] << "  " << Row[i] << "  " << Col[i] << "  " << Weight[i] <<endl;
    }
    out.close();
  }
}


void sync_worker(int rank, Barrier& Computation_Barrier, Barrier& Cache_Update_Barrier) {
  double block_size = double(m)/double(num_workers);
  Range range(int(ceil(rank * block_size)), int(ceil((rank + 1) * block_size)));
  if (rank == num_workers - 1) {
    range.end = m;
  }
  double block_size_2 = double(max_n)/double(num_workers);
  Range range_2(int(ceil(rank * block_size_2)), int(ceil((rank + 1) * block_size_2)));
  if (rank == num_workers - 1) {
    range_2.end = max_n;
  }

  int num_cords = range.end - range.start;
  int num_var = range_2.end - range_2.start;
  int index = 0;
  double temp;
  
  for (int i = 0; i < max_iter; i++) {
    for (int j = 0; j < num_cords; j++) {
      index = j + range.start;
      temp = s[Row[index]]-s[Col[index]] + Gamma[index] - 1;
      z[index] -= alpha * temp;
      double z_i = z[index];
      if (fabs(z_i) > 1){
        if (add_time[index] == 0)
          add_time[index] = i*alpha;
        if (z_i>1)
          Gamma[index] = (z_i-1) * kappa;
        else
          Gamma[index] = (z_i+1) * kappa;
      }
      temp = temp*kappa*alpha*Weight[index];
      update[Row[index]+rank*max_n] -= temp;
      update[Col[index]+rank*max_n] += temp;
    }
    Computation_Barrier.wait();
    for (int j = 0; j < num_var; j++) {
      index = j + range_2.start;
      for (int k = 0;k<num_workers;k++) {
        s[index] += update[index+k*max_n];
      }
    }
    Cache_Update_Barrier.wait();
  }
}


