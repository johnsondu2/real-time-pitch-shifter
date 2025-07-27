#include <stdio.h>

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

using namespace std;

#include "portaudio.h"
#include "smbPitchShift.hpp"

#define PASS 0
#define SHIFT 1

const int SAMPLING_FREQ = 44100;
const int BUFFER_SIZE = 128;  // have found that can go as low as 32
const int INPUT_CHANNEL_NO = 1;
const int OUTPUT_CHANNEL_NO = 1;
const PaSampleFormat SAMPLE_FORMAT = paFloat32;
const int NUM_THREADS = 4;
const int RING_SIZE = 16;  // larger size gives less crowding
volatile float SHIFT_AMOUNT = 1.0f;

mutex shift_mtx;  // mutex to protect shared resource of SHIFT_AMOUNT
atomic<bool> kill_thread(false);  // atomic flag to signal end of program

int state = PASS;  // initial state on entry of FSM is passthrough mode.
                   // note: this state variable is only accessed
                   // in one thread. no race condition exist

// track the status of each slot
enum class SlotStatus { EMPTY, FILLED, PROCESSED };

// define a ring buffer slot
struct ring_buffer_slot {
  float block[BUFFER_SIZE];
  mutex mtx;
  SlotStatus status = SlotStatus::EMPTY;
  condition_variable cv;
};

// make ring buffer to store blocks of data and their locks
ring_buffer_slot ring_buffer[RING_SIZE];

static void checkErr(PaError err) {
  if (err != paNoError) {
    printf("PortAudio error: %s\n", Pa_GetErrorText(err));
    exit(EXIT_FAILURE);
  }
}

// function ran by reader thread to read blocks from the stream
void ReadStream(PaStream* stream, ring_buffer_slot* ring_buffer) {
  int read_index = 0;
  while (!kill_thread) {
    unique_lock<mutex> lock(ring_buffer[read_index].mtx);  // lock

    // unlock and wait till notified AND either slot empty or user enters 'q'
    ring_buffer[read_index].cv.wait(lock, [&ring_buffer, read_index]() -> bool {
      return ring_buffer[read_index].status == SlotStatus::EMPTY || kill_thread;
    });

    if (kill_thread) break;  // end loop if user requested

    PaError err =
        Pa_ReadStream(stream, ring_buffer[read_index].block, BUFFER_SIZE);
    checkErr(err);

    // update the slot status, notify thread waiting, and move to next slot
    ring_buffer[read_index].status = SlotStatus::FILLED;
    ring_buffer[read_index].cv.notify_one();
    read_index = (read_index + 1) % RING_SIZE;
  }
}

// function ran by procssing thread to apply pitch shift to a block
void ProcessBlock(ring_buffer_slot* ring_buffer) {
  int process_index = 0;

  while (!kill_thread) {
    // unique lock, as we have to use the wait() function
    unique_lock<mutex> lock(ring_buffer[process_index].mtx);

    /* thread waits for a notification given by reader thread. if either the
     * status is FILLED or the kill_thread variable is true and we are notified,
     * then stop waiting and continue running thru the loop. */

    ring_buffer[process_index].cv.wait(
        lock, [&ring_buffer, process_index]() -> bool {
          return ring_buffer[process_index].status == SlotStatus::FILLED ||
                 kill_thread;
        });

    if (kill_thread) break;  // end loop if user wants to end program

    // this allows reading of shift amount safely, then copies the value such
    // that the rest of the threads are not blocked

    float current_shift;
    {
      lock_guard<mutex> lock(shift_mtx);  // lock
      current_shift = SHIFT_AMOUNT;       // copy SHIFT_AMOUNT,
    }  // unlock

    // By copying SHIFT_AMOUNT into a local variable, we minimise hold‐time on
    // shift_mtx so that the user‐input thread isn’t blocked while pitch
    // shifting.

    smbPitchShift(current_shift, BUFFER_SIZE, 1024, 32, SAMPLING_FREQ,
                  ring_buffer[process_index].block,
                  ring_buffer[process_index].block);

    // update the slot status, notify thread waiting, and move to next slot
    ring_buffer[process_index].status = SlotStatus::PROCESSED;
    ring_buffer[process_index].cv.notify_one();
    process_index = (process_index + 1) % RING_SIZE;
  }
}

void WriteBlock(PaStream* stream, ring_buffer_slot* ring_buffer) {
  int write_index = 0;
  while (!kill_thread) {
    unique_lock<mutex> lock(ring_buffer[write_index].mtx);

    ring_buffer[write_index].cv.wait(
        lock, [&ring_buffer, write_index]() -> bool {
          return ring_buffer[write_index].status == SlotStatus::PROCESSED ||
                 kill_thread;
        });

    if (kill_thread) break;

    // write to the stream

    PaError err =
        Pa_WriteStream(stream, ring_buffer[write_index].block, BUFFER_SIZE);
    checkErr(err);

    ring_buffer[write_index].status = SlotStatus::EMPTY;
    ring_buffer[write_index].cv.notify_one();
    write_index = (write_index + 1) % RING_SIZE;
  }
}

void new_state(int ns) {
  state = ns;
  if (ns == PASS) cout << "Now in Passthrough Mode" << endl;
  if (ns == SHIFT) cout << "Now in Pitch Shift Mode" << endl;
}

void end_program() {
  kill_thread = true;

  // go through entire ring buffer and make condition variable wake all threads
  // waiting on it. this enables all threads proceed to end their loops as
  // kill_thread is true and we notify all.
  for (int i = 0; i < RING_SIZE; i++) {
    ring_buffer[i].cv.notify_all();
  }
  return;
}

void GetUserInput() {
  while (1) {
    char key_in = getchar();
    switch (state) {
      // in passthrough mode
      case PASS:

        // switch to pitch shift mode on 's'
        if (key_in == 's') {
          new_state(SHIFT);
        }

        if (key_in == 'q') {
          end_program();
          return;
        }
        break;

      // in pitch shift mode
      case SHIFT:

        // switch to passthrough mode on 'p'
        if (key_in == 'p') {
          new_state(PASS);
          // as we update shared resource SHIFT_AMOUNT, must lock
          lock_guard<mutex> lock(shift_mtx);
          SHIFT_AMOUNT = 1.0;
        }

        // increase pitch on 'u' upto 2.0
        if (key_in == 'u') {
          // as we update shared resource SHIFT_AMOUNT, must lock
          lock_guard<mutex> lock(shift_mtx);
          if (SHIFT_AMOUNT < 2.0) {
            SHIFT_AMOUNT += 0.5;
            cout << "SHIFT_AMOUNT now: " << SHIFT_AMOUNT << endl;
          } else {
            cout << "Pitch shift limit reached" << endl;
          }
        }

        // decrease pitch on 'd' down to 0.5
        if (key_in == 'd') {
          // as we update shared resource SHIFT_AMOUNT, must lock
          lock_guard<mutex> lock(shift_mtx);
          if (SHIFT_AMOUNT > 0.5) {
            SHIFT_AMOUNT -= 0.5;
            cout << "SHIFT_AMOUNT now: " << SHIFT_AMOUNT << endl;
          } else {
            cout << "Pitch shift limit reached" << endl;
          }
        }

        if (key_in == 'q') {
          end_program();
          return;
        }
        break;
    }
  }
}

int main() {
  // portAudio setup
  PaStream* stream;
  PaError err;
  err = Pa_Initialize();
  checkErr(err);
  int defaultInputDevice = Pa_GetDefaultInputDevice();
  int defaultOutputDevice = Pa_GetDefaultOutputDevice();

  if (defaultInputDevice == paNoDevice || defaultOutputDevice == paNoDevice) {
    cerr << "No default input or output device found." << endl;
    Pa_Terminate();
    return -1;
  }

  err = Pa_OpenDefaultStream(&stream, INPUT_CHANNEL_NO, OUTPUT_CHANNEL_NO,
                             SAMPLE_FORMAT, SAMPLING_FREQ,
                             BUFFER_SIZE,  // Frames per buffer
                             nullptr,      // No callback (using blocking API)
                             nullptr);     // No user data
  checkErr(err);
  err = Pa_StartStream(stream);

  cout << "Program Started in Passthrough mode" << endl;
  cout << "\t * Enter 'p' for Passthrough mode and 's' for Pitch Shift mode"
       << endl;
  cout << "\t * In Pitch Shift mode, enter 'u' or 'd' to increase/decrease the "
          "shifted pitch"
       << endl;
  cout << "\t * Enter 'q' at any time to quit the program" << endl;

  // start threads
  thread threads[NUM_THREADS];

  threads[0] = thread(ReadStream, stream, ring_buffer);  // reader
  threads[1] = thread(ProcessBlock, ring_buffer);        // processor
  threads[2] = thread(WriteBlock, stream, ring_buffer);  // writer
  threads[3] = thread(GetUserInput);                     // user input

  // join threads
  for (int i = 0; i < NUM_THREADS; ++i) threads[i].join();

  cout << "Program End" << endl;
  err = Pa_StopStream(stream);
  checkErr(err);
  err = Pa_CloseStream(stream);
  checkErr(err);
  Pa_Terminate();
  return 0;
}
