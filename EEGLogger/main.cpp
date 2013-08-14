#include <iostream>
#include <fstream>
#include <conio.h>
#include <sstream>
#include <windows.h>
#include <map>
#include <math.h>
#include <complex>
#include <valarray>

#include "EmoStateDLL.h"
#include "edk.h"
#include "edkErrorCode.h"

#include "fftw3.h"

#pragma comment(lib, "../lib/edk.lib")
#pragma comment(lib, "../lib/libfftw3-3.lib")
#pragma comment(lib, "../lib/libfftw3f-3.lib")
#pragma comment(lib, "../lib/libfftw3l-3.lib")

const double PI = 3.141592653589793238460;

typedef std::complex<double> Complex;
typedef std::valarray<Complex> CArray;

EE_DataChannel_t targetChannelList[] = {
		ED_P7, ED_O1, ED_O2, ED_P8
	};

const char header[] = "P7, O1, O2, P8";

/* I cleaned up the code a bit and dumped it all into a thread.
 * This way, we can put this anywhere in SumatraPDF that we know will be called and start the thread.
 * No need to find a main loop.
 * Although, we may want to use something other than the keyboard to interrupt the thread; it still uses while(!kbhit()).
 */
public ref class EEGLoggerThread
{
public:
	static void ThreadProc()
	{
		// Instance variables.
		fftw_plan p;
		EmoEngineEventHandle eEvent			= EE_EmoEngineEventCreate();
		EmoStateHandle eState				= EE_EmoStateCreate();
		unsigned int userID					= 0;
		const unsigned short composerPort	= 1726;
		float secs							= 1;
		int bufferLength					= 20;
		unsigned int datarate				= 0;
		bool readytocollect					= false;
		int option							= 0;
		int state							= 0;
		int numLines						= 0;
		int Hz								= 10;
		int Hz2								= 8;
		bool foundHz						= false;
		bool foundHz2						= false;
		int sleepDelay = 1000; //Buffer time in ms. Will write new contents to ofs every cycle.
		int numRuns = 0;

		// Raw signal variables.
		int sampleSize = 0;
		double* P7Arr = new double[1];
		double* O1Arr = new double[1];
		double* O2Arr = new double[1];
		double* P8Arr = new double[1];

		double* P7Totals = new double[bufferLength * 128];
		double* P8Totals = new double[bufferLength * 128];
		double* O1Totals = new double[bufferLength * 128];
		double* O2Totals = new double[bufferLength * 128];

		std::string input;

		try {
			// Connect to the EE_Engine.
			if (EE_EngineConnect() != EDK_OK) {
				throw std::exception("Emotiv Engine start up failed.");
			}

			// Record starting and open the log files.
			std::cout << "Start receiving EEG Data! Press any key to stop logging...\n" << std::endl;
    		std::ofstream ofs("alog.log",std::ios::trunc);
			std::ofstream ofs2("blog.log",std::ios::trunc);
			std::ofstream ofs3("valsLog2.log",std::ios::trunc);

			DataHandle hData = EE_DataCreate();
			EE_DataSetBufferSizeInSec(secs);

			std::cout << "Buffer size in secs:" << secs << std::endl;

			// Repeat until a keyboard press is detected.
			while (!_kbhit()) {

				state = EE_EngineGetNextEvent(eEvent);

				if (state == EDK_OK) {

					EE_Event_t eventType = EE_EmoEngineEventGetType(eEvent);
					EE_EmoEngineEventGetUserId(eEvent, &userID);

					// Log the EmoState if it has been updated
					if (eventType == EE_UserAdded) {
						std::cout << "User added";
						EE_DataAcquisitionEnable(userID,true);
						readytocollect = true;
					}
				}

				if (readytocollect) {
							//Close and reopen output stream. so we only maintain previous buffer.
							ofs.close();
							ofs2.close();
							std::ofstream ofs("alog.log",std::ios::trunc);
							std::ofstream ofs2("blog.log",std::ios::trunc);

							EE_DataUpdateHandle(0, hData);

							unsigned int nSamplesTaken=0;
							EE_DataGetNumberOfSample(hData,&nSamplesTaken);
		
							std::cout << "Updated " << nSamplesTaken << std::endl;
							std::cout << "----------" << std::endl;

							if (nSamplesTaken != 0) {
								numLines += nSamplesTaken;
								sampleSize = nSamplesTaken;
								double* data = new double[nSamplesTaken];

								// Arrays to hold each electrode's data.
								P7Arr = new double[nSamplesTaken];
								O1Arr = new double[nSamplesTaken];
								O2Arr = new double[nSamplesTaken];
								P8Arr = new double[nSamplesTaken];

								// Throw out the last second of data.
								for (int i = (128 * bufferLength) - 129; i >= 0; i--)
								{
									P7Totals[i + 128] = P7Totals[i];
									P8Totals[i + 128] = P8Totals[i];
									O1Totals[i + 128] = O1Totals[i];
									O2Totals[i + 128] = O2Totals[i];
								}

								for (int sampleIdx=0 ; sampleIdx<(int)nSamplesTaken ; ++ sampleIdx) {
									for (int i = 0 ; i<sizeof(targetChannelList)/sizeof(EE_DataChannel_t) ; i++) {

										EE_DataGet(hData, targetChannelList[i], data, nSamplesTaken);
										//ofs << data[sampleIdx] << ",";
										// Put the data into the corresponding electrode array.
										if (i == 0)
											P7Arr[sampleIdx] = data[sampleIdx];
										else if (i == 1)
											O1Arr[sampleIdx] = data[sampleIdx];
										else if (i == 2)
											O2Arr[sampleIdx] = data[sampleIdx];
										else
											P8Arr[sampleIdx] = data[sampleIdx];
										// Add to the totals arrays.
										P7Totals[sampleIdx] = P7Arr[sampleIdx];
										P8Totals[sampleIdx] = P8Arr[sampleIdx];
										O1Totals[sampleIdx] = O1Arr[sampleIdx];
										O2Totals[sampleIdx] = O2Arr[sampleIdx];
										// Output the data to the command line, to see what the code has (or not).
										//std::cout << data[sampleIdx] << ",";
									}
									//ofs << std::endl;
								}
								delete[] data;
							}

							std::cout << "----------" << std::endl;
							int Fs = 128;

							//sampleSize = Fs * bufferLength;

							int N = Fs * bufferLength;

							// And now, process the data. (I know, very descriptive.)
							if (N > 0 && numRuns >= bufferLength)
							{
								ofs.close();
								ofs2.close();
								std::ofstream ofs("alog.log",std::ios::trunc);
								std::ofstream ofs2("blog.log",std::ios::trunc);
								
								for (int i = 0; i < bufferLength * 128; i++)
								{
									ofs << P7Totals[i] << "," << P8Totals[i] << "," << O1Totals[i] << "," << O2Totals[i] << "," << std::endl;
								}

								// Variables for peak detection.
								int detectIndex = (int)(Hz / ((double)Fs/(double)N) + 0.5) - 3;
								int downIndices = detectIndex - 3;
								int upIndices = detectIndex + 3;
								int detectIndex2 = (int)(Hz2 / ((double)Fs/(double)N) + 0.5) - 3;
								int downIndices2 = detectIndex - 3;
								int upIndices2 = detectIndex + 3;
								double P7Vals[] = {0, 0, 0};
								double P8Vals[] = {0, 0, 0};
								double O1Vals[] = {0, 0, 0};
								double O2Vals[] = {0, 0, 0};

								// Setup & do the FFT.
								double *input = new double[N];
								input = P7Totals;
								fftw_complex *outP7 = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
								p = fftw_plan_dft_r2c_1d(N, input, outP7, FFTW_ESTIMATE);
								fftw_execute(p);

								input = P8Totals;
								fftw_complex *outP8 = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
								p = fftw_plan_dft_r2c_1d(N, input, outP8, FFTW_ESTIMATE);
								fftw_execute(p);
								
								input = O1Totals;
								fftw_complex *outO1 = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
								p = fftw_plan_dft_r2c_1d(N, input, outO1, FFTW_ESTIMATE);
								fftw_execute(p);

								input = O2Totals;
								fftw_complex *outO2 = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * N);
								p = fftw_plan_dft_r2c_1d(N, input, outO2, FFTW_ESTIMATE);
								fftw_execute(p);

								// These arrays will hold the fft'd data and x-axis (frequency)
								double* frequencies = new double[N];
								double* P7Mags = new double[N];
								double* P8Mags = new double[N];
								double* O1Mags = new double[N];
								double* O2Mags = new double[N];

								for (int k = 0; k < N; k++)
								{
									// Compute the frequency & magnitude of the fft output (it'll be complex)...
									double freq = ((k) / (double)(N / Fs));
									double magP7 = sqrt(outP7[k][0] * outP7[k][0] + outP7[k][1] * outP7[k][1]) / N;
									double magP8 = sqrt(outP8[k][0] * outP8[k][0] + outP8[k][1] * outP8[k][1]) / N;
									double magO1 = sqrt(outO1[k][0] * outO1[k][0] + outO1[k][1] * outO1[k][1]) / N;
									double magO2 = sqrt(outO2[k][0] * outO2[k][0] + outO2[k][1] * outO2[k][1]) / N;

									// ...and put it into the corresponding arrays.
									frequencies[k] = freq;
									P7Mags[k] = magP7;
									P8Mags[k] = magP8;
									O1Mags[k] = magO1;
									O2Mags[k] = magO2;

									// (Output the values to the appropriate log file.)
									ofs2 << freq << "," << magP7 << "," <<  magO1 << "," << magO2 << "," << magP8 << "," << std::endl;
								}

								// Detect the most likely peak location within +/- 3 bins of where we're looking.
								double O2Val = O2Mags[detectIndex];
								for (int i = detectIndex; i < detectIndex + 6; i++)
								{
									if (O2Mags[i] > O2Val)
									{
										O2Val = O2Mags[i];
										detectIndex = i;
									}
								}

								// Output where that is.
								std::cout << "Biggest peak for " << Hz << " at: " << frequencies[detectIndex] << " Hz" << std::endl;

								// Adjust the peak detection variables.
								downIndices = detectIndex - 3;
								upIndices = detectIndex + 3;

								// Output the indices.
								std::cout << downIndices << ", " << detectIndex << ", " << upIndices << std::endl;

								// Define the shitload of peak detection variables.
								P7Vals[0] = P7Mags[downIndices];
								P8Vals[0] = P8Mags[downIndices];
								O1Vals[0] = O1Mags[downIndices];
								O2Vals[0] = O2Mags[downIndices];
								P7Vals[1] = P7Mags[detectIndex];
								P8Vals[1] = P8Mags[detectIndex];
								O1Vals[1] = O1Mags[detectIndex];
								O2Vals[1] = O2Mags[detectIndex];
								P7Vals[2] = P7Mags[upIndices];
								P8Vals[2] = P8Mags[upIndices];
								O1Vals[2] = O1Mags[upIndices];
								O2Vals[2] = O2Mags[upIndices];

								double O1Avgs = (O1Vals[0] + O1Vals[2]) / 2;
								double O1Diff = O1Vals[1] - O1Avgs;
								double O1Div = (double)O1Vals[1] / (double)O1Avgs;
								double O2Avgs = (O2Vals[0] + O2Vals[2]) / 2;
								double O2Diff = O2Vals[1] - O2Avgs;
								double O2Div = (double)O2Vals[1] / (double)O2Avgs;
								double P7Avgs = (P7Vals[0] + P7Vals[2]) / 2;
								double P7Diff = P7Vals[1] - P7Avgs;
								double P7Div = (double)P7Vals[1] / (double)P7Avgs;
								double P8Avgs = (P8Vals[0] + P8Vals[2]) / 2;
								double P8Diff = P8Vals[1] - P8Avgs;
								double P8Div = (double)P8Vals[1] / (double)P8Avgs;

								// Decide whether there is a peak, and output appropriate data.
								if (O2Diff > 2.5 && (double) O2Div > 2.5)
								{
									std::cout << "You're looking at a " << Hz << "Hz signal...maybe." << std::endl;
									foundHz = true;
									std::cout<< "\a";
									std::cout << "Difference: " << O2Diff << ", Divided: " << O2Div << std::endl << "[0]: " << O2Vals[0] << ", [1]: " << O2Vals[1] << ", [2]: " << O2Vals[2] << std::endl;
									ofs3 << "O1: " << O1Diff << "    " << O1Div << std::endl << "O1: " << O1Vals[0] << "    " << O1Vals[1] << "    " << O1Vals[2] << std::endl;
									ofs3 << "O2: " << O2Diff << "    " << O2Div << std::endl << "O2: " << O2Vals[0] << "    " << O2Vals[1] << "    " << O2Vals[2] << std::endl;
									ofs3 << "P7: " << P7Diff << "    " << P7Div << std::endl << "P7: " << P7Vals[0] << "    " << P7Vals[1] << "    " << P7Vals[2] << std::endl;
									ofs3 << "P8: " << P8Diff << "    " << P8Div << std::endl << "P8: " << P8Vals[0] << "    " << P8Vals[1] << "    " << P8Vals[2] << std::endl;
									std::cout << "----------------------" << std::endl;
									ofs3 << std::endl;
								}
								else
								{
									std::cout << "Difference: " << O2Diff << ", Divided: " << O2Div << std::endl << "[0]: " << O2Vals[0] << ", [1]: " << O2Vals[1] << ", [2]: " << O2Vals[2] << std::endl;
									foundHz = false;
									ofs3 << "O1: " << O1Diff << "    " << O1Div << std::endl << "O1: " << O1Vals[0] << "    " << O1Vals[1] << "    " << O1Vals[2] << std::endl;
									ofs3 << "O2: " << O2Diff << "    " << O2Div << std::endl << "O2: " << O2Vals[0] << "    " << O2Vals[1] << "    " << O2Vals[2] << std::endl;
									ofs3 << "P7: " << P7Diff << "    " << P7Div << std::endl << "P7: " << P7Vals[0] << "    " << P7Vals[1] << "    " << P7Vals[2] << std::endl;
									ofs3 << "P8: " << P8Diff << "    " << P8Div << std::endl << "P8: " << P8Vals[0] << "    " << P8Vals[1] << "    " << P8Vals[2] << std::endl;
									std::cout << "----------------------" << std::endl;
									ofs3 << std::endl;
								}

								// Rinse & repeat for the 2nd signal we're looking for.
								O2Val = O2Mags[detectIndex2];
								for (int i = detectIndex2; i < detectIndex2 + 6; i++)
								{
									if (O2Mags[i] > O2Val)
									{
										O2Val = O2Mags[i];
										detectIndex2 = i;
									}
								}

								std::cout << "Biggest peak for " << Hz2 << " at: " << frequencies[detectIndex2] << " Hz" << std::endl;

								downIndices2 = detectIndex2 - 3;
								upIndices2 = detectIndex2 + 3;

								std::cout << downIndices2 << ", " << detectIndex2 << ", " << upIndices2 << std::endl;
								
								P7Vals[0] = P7Mags[downIndices2];
								P8Vals[0] = P8Mags[downIndices2];
								O1Vals[0] = O1Mags[downIndices2];
								O2Vals[0] = O2Mags[downIndices2];
								P7Vals[1] = P7Mags[detectIndex2];
								P8Vals[1] = P8Mags[detectIndex2];
								O1Vals[1] = O1Mags[detectIndex2];
								O2Vals[1] = O2Mags[detectIndex2];
								P7Vals[2] = P7Mags[upIndices2];
								P8Vals[2] = P8Mags[upIndices2];
								O1Vals[2] = O1Mags[upIndices2];
								O2Vals[2] = O2Mags[upIndices2];
							
								O1Avgs = (O1Vals[0] + O1Vals[2]) / 2;
								O1Diff = O1Vals[1] - O1Avgs;
								O1Div = (double)O1Vals[1] / (double)O1Avgs;
								O2Avgs = (O2Vals[0] + O2Vals[2]) / 2;
								O2Diff = O2Vals[1] - O2Avgs;
								O2Div = (double)O2Vals[1] / (double)O2Avgs;
								P7Avgs = (P7Vals[0] + P7Vals[2]) / 2;
								P7Diff = P7Vals[1] - P7Avgs;
								P7Div = (double)P7Vals[1] / (double)P7Avgs;
								P8Avgs = (P8Vals[0] + P8Vals[2]) / 2;
								P8Diff = P8Vals[1] - P8Avgs;
								P8Div = (double)P8Vals[1] / (double)P8Avgs;

								if (O2Diff > 2.5 && (double) O2Div > 2.5 && !foundHz)
								{
									std::cout << "You're looking at a " << Hz2 << "Hz signal...maybe." << std::endl;
									foundHz2 = true;
									std::cout << "Difference: " << O2Diff << ", Divided: " << O2Div << std::endl << "[0]: " << O2Vals[0] << ", [1]: " << O2Vals[1] << ", [2]: " << O2Vals[2] << std::endl;
									ofs3 << "O1: " << O1Diff << "    " << O1Div << std::endl << O1Vals[0] << "    " << O1Vals[1] << "    " << O1Vals[2] << std::endl;
									ofs3 << "O2: " << O2Diff << "    " << O2Div << std::endl << O2Vals[0] << "    " << O2Vals[1] << "    " << O2Vals[2] << std::endl;
									ofs3 << "P7: " << P7Diff << "    " << P7Div << std::endl << P7Vals[0] << "    " << P7Vals[1] << "    " << P7Vals[2] << std::endl;
									ofs3 << "P8: " << P8Diff << "    " << P8Div << std::endl << P8Vals[0] << "    " << P8Vals[1] << "    " << P8Vals[2] << std::endl;
									std::cout << "----------------------" << std::endl;
									ofs3 << std::endl;
								}
								else
								{
									std::cout << "Difference: " << O2Diff << ", Divided: " << O2Div << std::endl << "[0]: " << O2Vals[0] << ", [1]: " << O2Vals[1] << ", [2]: " << O2Vals[2] << std::endl;
									foundHz2 = false;
									ofs3 << "O1: " << O1Diff << "    " << O1Div << std::endl << O1Vals[0] << "    " << O1Vals[1] << "    " << O1Vals[2] << std::endl;
									ofs3 << "O2: " << O2Diff << "    " << O2Div << std::endl << O2Vals[0] << "    " << O2Vals[1] << "    " << O2Vals[2] << std::endl;
									ofs3 << "P7: " << P7Diff << "    " << P7Div << std::endl << P7Vals[0] << "    " << P7Vals[1] << "    " << P7Vals[2] << std::endl;
									ofs3 << "P8: " << P8Diff << "    " << P8Div << std::endl << P8Vals[0] << "    " << P8Vals[1] << "    " << P8Vals[2] << std::endl;
									std::cout << "----------------------" << std::endl;
									ofs3 << std::endl;
								}
							}
							else
								numRuns ++;
				}

				// Sleep for the specified time.
				Sleep(sleepDelay);
			
			}
			// (Total # of values received. It's fun to know, I guess.)
			std::cout<<numLines<<std::endl;

			// Close the files.
			ofs.close();
			ofs2.close();
			ofs3.close();
			EE_DataFree(hData);

		}
		catch (const std::exception& e) {
			std::cerr << e.what() << std::endl;
			std::cout << "Press any key to exit..." << std::endl;
			getchar();
		}

		// Shut down the engine.
		EE_EngineDisconnect();
		EE_EmoStateFree(eState);
		EE_EmoEngineEventFree(eEvent);

		// Yield the rest of the thread's time.
		System::Threading::Thread::Sleep( 0 );
	}
};

int main(int argc, char** argv) {
	// Create a new EEGThread.
	System::Threading::Thread^ EEGThread = gcnew System::Threading::Thread( gcnew System::Threading::ThreadStart( &EEGLoggerThread::ThreadProc ) );
	// Start the thread, and wait for it to finish before stopping.
	EEGThread->Start();
	EEGThread->Join();

	return 0;
}