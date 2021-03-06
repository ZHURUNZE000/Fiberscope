/*
Accessory Light modulator Package Wrapper (ALPwrapper)
Programmed by Shay Ohayon
DiCarlo Lab @ MIT

Revision History
Version 0.1 7/16/2014  

*/
#include <stdio.h>
#include <mex.h>
#include <alp.h>
#include <Windows.h>
#include <queue>
#include <list>

#define MIN(a,b) (a)<(b)?(a):(b)
const int MAX_CONNECTED_DMDS = 2;

class ALPwrapper {
public:
		ALPwrapper(int devID);
		~ALPwrapper();
		bool isInitialized();
		bool init();
		void release();
		bool clear(bool white);
		bool stopSequence();
		bool waitForSequenceCompletion();
		bool prepareCalibrationSequence(int numModes);
		bool hasSequenceCompleted();

		int uploadSequence(unsigned char *sequence, int numFrames);
		bool runUploadedSequence(int sequence, double frameRate, bool continuous, long numRepeats);
		bool releaseAllSequences();
		bool releaseSequence(int sequence);
		bool showPattern(unsigned char *pattern);
		unsigned char* packInput(unsigned char *Input, int Input_width, int Input_height, int numFrames);
		int allocateStandardSequence(int nFrames);
private:
	void removeAllocatedSequenceFromList(int sequence);
	bool playingCont;
	std::list<int> allocatedSequences;
	bool initialized;
	ALP_ID nAlpId;
	int _devID;
	int width, height;
};

ALPwrapper *alps[MAX_CONNECTED_DMDS];


bool ALPwrapper::isInitialized()
{
	return initialized;
}

bool ALPwrapper::init()
{
	if (initialized)
		return true;

	// Initialize ALP
	mexPrintf("ALP initializing device %d.\n", _devID);
	long nDmdType;
	int Res = AlpDevAlloc(_devID, ALP_DEFAULT, &nAlpId);
	if (ALP_OK !=  Res ||
		ALP_OK != AlpDevInquire( nAlpId, ALP_DEV_DMDTYPE, &nDmdType ))
	{
		mexPrintf( "ALP init error (%d).\n",Res );
		return false;
	} 

	switch (nDmdType) {
	case ALP_DMDTYPE_XGA_055A :
	case ALP_DMDTYPE_XGA_055X :
	case ALP_DMDTYPE_XGA_07A :
		width = 1024; 
		height = 768;
		break;
	case ALP_DMDTYPE_DISCONNECT :
	case ALP_DMDTYPE_1080P_095A :
		width = 1920; 
		height = 1080;
		break;
	case ALP_DMDTYPE_WUXGA_096A :
		width = 1920; 
		height = 1200;
		break;
	default:
		// unsupported DMD type
		return false;
	}

	initialized = true;
	return true;
}

ALPwrapper::ALPwrapper(int devID)
{
	_devID = devID;
	initialized = false;
	playingCont = false;
	nAlpId = -1;
}

ALPwrapper::~ALPwrapper()
{
	release();
}


bool ALPwrapper::prepareCalibrationSequence(int numModes)
{

	// Initialize calibration sequences, according to the number of input modes
	
	// The number of frames equals 3 or 4 times the number of modes, depending on which interference pattern is used.
	// Poppof proposes:
	// 
	// 
	int nFrames = 4 * numModes;

	ALP_ID nSeqId;
	if (ALP_OK != AlpSeqAlloc( nAlpId, 1, nFrames, &nSeqId ))
	{
		mexPrintf( "Error allocating memory for sequence on device\n" );
		return false;
	}
	allocatedSequences.push_back(nSeqId);

	// Set the data format as binary. This will save space and allow more sequences to be stored on the device.
	long Result1 = AlpSeqControl (nAlpId, nSeqId, ALP_SEQ_REPEAT, 1); // only run the calibraiton sequence once
	long Result2 = AlpSeqControl (nAlpId, nSeqId, ALP_BITNUM, 1); // binary patterns and not gray scale
	long Result3 = AlpSeqControl (nAlpId, nSeqId, ALP_FIRSTFRAME, 0); // binary patterns and not gray scale
	long Result4 = AlpSeqControl (nAlpId, nSeqId, ALP_LASTFRAME, nFrames-1); // binary patterns and not gray scale
	long Result5 = AlpSeqControl (nAlpId, nSeqId, ALP_DATA_FORMAT, ALP_DATA_BINARY_TOPDOWN);

	// Imporant.  To achieve maximal frame rate we switch to a binary mode that is uninterrupted by "dark phase"
	// "Dark phase" is usually used to initialize the next frame. However, in binary mode, no such preprocessing is needed!
	long Result6 = AlpSeqControl(nAlpId, nSeqId, ALP_BIN_MODE, ALP_BIN_UNINTERRUPTED);

	if (Result1 != ALP_OK || Result2 != ALP_OK || Result3 != ALP_OK || Result4 != ALP_OK || Result5 != ALP_OK || Result6 != ALP_OK)
	{
		AlpSeqFree( nAlpId, nSeqId );
		mexPrintf( "Error setting sequence control parameters\n" );
		return false;
	}

	long minPictureTime, minIlluminationTime;
	long Result7 = AlpSeqInquire(nAlpId, nSeqId, ALP_MIN_PICTURE_TIME, &minPictureTime);
	// Query what is the minimal picture time allowed.
	long Result8 = AlpSeqInquire(nAlpId, nSeqId, ALP_MIN_ILLUMINATE_TIME, &minIlluminationTime);
	

	// need to call AlpSeqTiming to allow new bit plane to take effect...

	// The PictureTime is the most important parameter for controlling frame rate.
	// It defines an interval that contains the actual display of one frame as well as all related trigger and synch processing
	// The frame display processing is done during a part of PictureTime called IlluminateTime
	// Afterwards it takes some time to initialize the next frame. During this time the DMD is cleared
	// This so-called dark phase determines the minimum difference Δt1 between IlluminateTime and PictureTime
	// that is, PictureTime-IlluminateTime≥Δt1
	// In the binary mode without dark phase (ALP_BIN_UNINTERRUPTED) the processing of a frame completes when it appears on the DMD. So in this mode the IlluminateTime is ignored
	
	// In master mode, the ALP displays frames and produces synch pulses solely based on internal timing. The TriggerInDelay setting is ignored
	// When using ALP_BINARY_UNINTERRUPTED mode, the default SynchPulseWidth is half of PictureTime.
	// Illumination is delayed from the beginning of the PictureTime interval by SynchDelay

	// The minimum dark phase for XGA DMDs (1024x768) is 44 uSec. However, if ALP_BINARY_UNINTERRUPTED is used, no dark phase is used.
	// 


	// Illumination time: The time the image is actually on the DMD.
	// duration of the display of one picture in the sequence in uSec
	// Default: (ALP_DEFAULT), highest possible contrast available for the specified PictureTime
	// This value will be ignored since we will use the binary uninterrrupted mode.
	long IlluminateTime = ALP_DEFAULT;  
						  

	/// Picture Time: time between the start of two consecutive pictures
	// If IlluminateTime is also ALP_DEFAULT then 33334 μs are used according to a frame rate of 30 Hz. 
	// Otherwise PictureTime is set to minimize the dark time according to the specified IlluminateTime.
	// Value in uSec.
	// Maximal refresh rate is 
	long PictureTime; 
	

	long SynchDelay; // specifies the time between start of the frame synch output pulse and the start of the display
	// in USec, Default: ALP_DEFAULT: 0


	long SynchPulseWidth; // specifies the duration of the frame synch output pulse.
	// ALP_DEFAULT	= TriggerInDelay + IlluminateTime in normal mode,

	long TriggerInDelay;  // specifies the time between the incoming trigger edge and the start of the display, ALP_DEFAULT: 0 usec
	
	long Result9 = AlpSeqTiming (nAlpId, nSeqId,  IlluminateTime, PictureTime, SynchDelay, SynchPulseWidth, TriggerInDelay);

	

	// Allocate memory for sequence on host computer
	UCHAR *pImageData = new UCHAR[nFrames*width*height/8];
	if (pImageData == nullptr) 
	{
		mexPrintf( "Error allocating memory for sequence on host computer\n" );
		AlpSeqFree( nAlpId, nSeqId );
		return false;
	}
	// remember that data is passed as strides. That is, the first byte corresponds to the first 8 pixels.
	// The left most bit (MSB) is the first pixel.


	//FillMemory( pImageData, nSizeX*nSizeY, 0x80 );				// white
	//FillMemory( pImageData+nSizeX*nSizeY, nSizeX*nSizeY, 0x00 );		// black
	int nReturn = AlpSeqPut( nAlpId, nSeqId, 0, nFrames, pImageData ); // BLOCKING (!)


	delete pImageData;

	return true;
}


void ALPwrapper::release()
{
	if (!initialized)
		return;

	AlpDevHalt( nAlpId );
	releaseAllSequences();
	AlpDevFree( nAlpId );

	initialized = false;
}

bool ALPwrapper::stopSequence()
{
	int Ret1 = AlpProjHalt(nAlpId); // non-blocking. Request sequence halt
	int Ret2 = AlpProjWait(nAlpId); // wait for sequence to end, then return.
	playingCont = false;
	return Ret1 == ALP_OK && Ret2 == ALP_OK;
}

bool ALPwrapper::hasSequenceCompleted()
{
	long Ret;
	int Ret2 = AlpProjInquire(nAlpId,ALP_PROJ_STATE,&Ret); // wait for sequence to end, then return.
	return Ret == ALP_PROJ_IDLE;

}


bool ALPwrapper::waitForSequenceCompletion()
{
	int Ret2 = AlpProjWait(nAlpId); // wait for sequence to end, then return.
	return Ret2 == ALP_OK;
}
int ALPwrapper::allocateStandardSequence(int nFrames)
{
	ALP_ID nSeqId;
	if (ALP_OK != AlpSeqAlloc(nAlpId, 1, nFrames, &nSeqId))
	{
		mexPrintf("Error allocating memory for sequence on device\n");
		return -1;
	}
	// Set the data format as binary. This will save space and allow more sequences to be stored on the device.
	long Result1 = AlpSeqControl(nAlpId, nSeqId, ALP_SEQ_REPEAT, 1); // only run the calibraiton sequence once
	long Result2 = AlpSeqControl(nAlpId, nSeqId, ALP_BITNUM, 1); // binary patterns and not gray scale
	long Result3 = AlpSeqControl(nAlpId, nSeqId, ALP_FIRSTFRAME, 0); // binary patterns and not gray scale
	long Result4 = AlpSeqControl(nAlpId, nSeqId, ALP_LASTFRAME, nFrames - 1); // binary patterns and not gray scale
	long Result5 = AlpSeqControl(nAlpId, nSeqId, ALP_DATA_FORMAT, ALP_DATA_BINARY_TOPDOWN);

	// Imporant.  To achieve maximal frame rate we switch to a binary mode that is uninterrupted by "dark phase"
	// "Dark phase" is usually used to initialize the next frame. However, in binary mode, no such preprocessing is needed!
	long Result6 = AlpSeqControl(nAlpId, nSeqId, ALP_BIN_MODE, ALP_BIN_UNINTERRUPTED);

	if (Result1 != ALP_OK || Result2 != ALP_OK || Result3 != ALP_OK || Result4 != ALP_OK || Result5 != ALP_OK || Result6 != ALP_OK)
	{
		AlpSeqFree(nAlpId, nSeqId);
		mexPrintf("Error setting sequence control parameters\n");
		return -1;
	}


	// Use SYNCH_OUT lines? Not very useful at the moment
	/*
	// set SYNC_OUT1 gate to trigger for the first frame in a sequence.
	tAlpDynSynchOutGate Gate;
	ZeroMemory(&Gate, 18); // 18 = sizeof(tAlpDynSynchOutGate)
	Gate.Period = nFrames-1;
	Gate.Polarity = 1;
	Gate.Gate[0] = 1; 
	int res = AlpDevControlEx(nAlpId, ALP_DEV_DYN_SYNCH_OUT1_GATE, &Gate);
	// Period and Polarity stays the same, update Gate setting for second port:
	Gate.Gate[0] = 0; Gate.Gate[1] = 1;
	AlpDevControlEx(nAlpId, ALP_DEV_DYN_SYNCH_OUT2_GATE, &Gate);
	// Update Gate setting for third port:
	Gate.Gate[1] = 0; Gate.Gate[2] = 1;
	AlpDevControlEx(nAlpId, ALP_DEV_DYN_SYNCH_OUT3_GATE, &Gate);
	if (res != ALP_OK)
	{
		mexPrintf("Error setting sequence extra control parameters\n");
		return -1;
	}
	*/

	allocatedSequences.push_back(nSeqId);
	return nSeqId;
}

bool ALPwrapper::showPattern(unsigned char *pattern)
{

	int nSeqId = allocateStandardSequence(1);
	if (nSeqId == -1)
	{
		mexPrintf("Error allocating sequence \n");
		return false;
	}

	int nReturn = AlpSeqPut(nAlpId, nSeqId, 0, ALP_DEFAULT, pattern);
	if (nReturn != ALP_OK)
	{
		releaseSequence(nSeqId);
		mexPrintf("Error placing sequence in memory\n");
		return false;
	}

	int StartSuccessfuly = AlpProjStartCont(nAlpId, nSeqId);

	return StartSuccessfuly == ALP_OK ;
}




bool ALPwrapper::clear(bool white)
{
	int nSeqId = allocateStandardSequence(1);
	if (nSeqId == -1)
	{
		mexPrintf("Error allocating sequence \n");
		return false;
	}

	UCHAR *pImageData = new UCHAR[width*height / 8];
	if (white)
		FillMemory(pImageData, width*height / 8, 255);		// black
	else
		FillMemory(pImageData, width*height / 8, 0x00);		// black

	int nReturn = AlpSeqPut(nAlpId, nSeqId, 0, 1, pImageData); // BLOCKING (!)

	delete pImageData;

	if (nReturn != ALP_OK)
	{
		return false;
	}
	int StartSuccessfuly = AlpProjStartCont(nAlpId, nSeqId);

	return StartSuccessfuly == ALP_OK;


}


void ALPwrapper::removeAllocatedSequenceFromList(int sequence)
{
	allocatedSequences.remove(sequence);
}


int ALPwrapper::uploadSequence(unsigned char *sequence, int numFrames)
{
	int nSeqId = allocateStandardSequence(numFrames);
	if (nSeqId == -1)
	{
		mexPrintf("Error allocating sequence \n");
		return -1;
	}

	int nReturn = AlpSeqPut(nAlpId, nSeqId, 0, ALP_DEFAULT, sequence);
	if (nReturn != ALP_OK)
	{
		releaseSequence(nSeqId);		
		mexPrintf("Error placing sequence in memory\n");
		return -1;
	}
	return nSeqId;
}

bool ALPwrapper::runUploadedSequence(int sequence, double frameRate, bool continuous, long numRepeats=1)
{
	// Verify that the sequence was actually allocated...
	for (std::list<int>::iterator it = allocatedSequences.begin(); it != allocatedSequences.end(); it++)
	{
		if (*it == sequence)
		{


			long IlluminateTime = 0; // Ignored, we are in uninterrupted binary mode
			long PictureTime = 1.0 / double(frameRate) * 1000000; // in uSecs
			long SynchDelay = 0;
			long SynchPulseWidth = PictureTime / 2;
			long TriggerInDelay = 0;

/*			long minPictureTime, minIlluminationTime, SyncPulseWidth, SyncDelay, TriggerDelay;
			long Result7 = AlpSeqInquire(nAlpId, nSeqId, ALP_MIN_PICTURE_TIME, &minPictureTime);
			long Result8 = AlpSeqInquire(nAlpId, nSeqId, ALP_MIN_ILLUMINATE_TIME, &minIlluminationTime);
			long Result9 = AlpSeqInquire(nAlpId, nSeqId, ALP_SYNCH_DELAY, &SyncDelay);
			long Result10 = AlpSeqInquire(nAlpId, nSeqId, ALP_SYNCH_PULSEWIDTH, &SyncPulseWidth);
			long Result11 = AlpSeqInquire(nAlpId, nSeqId, ALP_TRIGGER_IN_DELAY, &TriggerDelay);*/
			if (!continuous)
			{
				long Res= AlpSeqControl(nAlpId, sequence, ALP_SEQ_REPEAT, numRepeats); // only run the calibraiton sequence once
			}
			long Result9 = AlpSeqTiming(nAlpId, sequence, IlluminateTime, PictureTime, SynchDelay, SynchPulseWidth, TriggerInDelay);
			if (Result9 == ALP_OK)
			{
				int StartSuccessfuly;
				if (continuous) {
					if (playingCont)
					{
						 // sequence is already playing. Stop it first.
						stopSequence();
					}
					playingCont = true;
					StartSuccessfuly  = AlpProjStartCont(nAlpId, *it);
				}
				else {
					StartSuccessfuly = AlpProjStart(nAlpId, *it);
					playingCont = false;
				}

				return StartSuccessfuly == ALP_OK;
			}
			else
			{
				return false;
			}
		}
	}
	return false;
}

bool ALPwrapper::releaseAllSequences()
{
	bool allSuccessful = true;
	for (std::list<int>::iterator it = allocatedSequences.begin(); it != allocatedSequences.end(); it++)
	{
		int retValue = AlpSeqFree(nAlpId, *it);
		allSuccessful = allSuccessful && retValue == ALP_OK;
	}
	allocatedSequences.clear();
	return allSuccessful;
}

bool ALPwrapper::releaseSequence(int sequence)
{
	int retValue = AlpSeqFree(nAlpId, sequence);
	removeAllocatedSequenceFromList(sequence);
	return retValue == ALP_OK;
}



unsigned char* ALPwrapper::packInput(unsigned char *Input, int Input_width, int Input_height, int numFrames)
{
	// converts input with various sizes to standard packed binary format
	int stride = width / 8;
	size_t memToAllocate = stride * height * numFrames;
	unsigned char *packedInput = new unsigned char[memToAllocate];
	memset(packedInput, 0, width / 8 * height);

	if (Input_width == 1024 || Input_width == 768)
	{
		for (int frame = 0; frame < numFrames; frame++)
		{
			size_t packedFrameOffset = frame * stride * height;
			size_t InputFrameOffset = Input_width * Input_height * frame;
			// Unpacked bytes. need to unpack
			int dim = Input_width;
			int len = Input_width / 8;
			for (int y = 0; y < height; y++)
			{
				for (int x = 0; x < len; x++)
				{

					packedInput[packedFrameOffset + y * stride + x] = Input[InputFrameOffset + height * (x * 8 + 0) + y] * 128 | Input[InputFrameOffset + height * (x * 8 + 1) + y] * 64 |
						Input[InputFrameOffset + height * (x * 8 + 2) + y] * 32 | Input[InputFrameOffset + height * (x * 8 + 3) + y] * 16 | Input[InputFrameOffset + height * (x * 8 + 4) + y] * 8 |
						Input[InputFrameOffset + height * (x * 8 + 5) + y] * 4 | Input[InputFrameOffset + height * (x * 8 + 6) + y] * 2 | Input[InputFrameOffset + height * (x * 8 + 7) + y] * 1;
				}
			}
		}
	} else
	{
		// Assume Input width of 128 (i.e., already packed...)
		memcpy(packedInput, Input, stride*height*numFrames);
	}
	
	return packedInput;
}


void ShowPattern(int devID, int nlhs, mxArray *plhs[],	int nrhs, const mxArray *prhs[])
{
	

	if (nrhs != 3)
	{
		mexErrMsgTxt("Need an input pattern");
		plhs[0] = mxCreateDoubleScalar(false);
		return;
	}

	const int *dim1 = mxGetDimensions(prhs[2]);
	if (dim1[0] != 768)
	{
		mexErrMsgTxt("Pattern needs to be 768x128 (packed) or 768x1024 (unpacked, binary), UINT8");
		return;
	}

	if (dim1[1] != 128 && dim1[1] != 1024 && dim1[1] != 768)  {
		mexErrMsgTxt("Pattern needs to be 768x128 (1024 packed) or 768x1024 (unpacked, binary), or 768x768 (unpacked, binary) UINT8");
		return;
	}

	if (!mxIsUint8(prhs[2]) && !mxIsLogical(prhs[2]))
	{
		mexErrMsgTxt("Pattern needs to be 768x128 (packed) or 768x1024 (unpacked, binary), UINT8");
		return;
	}

	unsigned char *Input = (unsigned char *)mxGetData(prhs[2]);
	if (alps[devID] != nullptr)
	{
		unsigned char *Pat = alps[devID]->packInput(Input, dim1[1], dim1[0], 1);
		bool Success = alps[devID]->showPattern(Pat);
		plhs[0] = mxCreateDoubleScalar(Success);
		delete Pat;
	}
	else
	{
		plhs[0] = mxCreateDoubleScalar(false);
	}
}


void UploadPatternSequence(int devID, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	if (nrhs != 3)
	{
		mexErrMsgTxt("Need an input pattern sequence");
		plhs[0] = mxCreateDoubleScalar(false);
		return;
	}
	

	const int numDim = mxGetNumberOfDimensions(prhs[2]);
	const int *dim1 = mxGetDimensions(prhs[2]);
	int numFrames = (numDim == 3) ? dim1[2] : 1;
	if ((dim1[0] != 768) || (dim1[1] != 128 && dim1[1] != 1024 && dim1[1] != 768) || (!mxIsUint8(prhs[2]) && !mxIsLogical(prhs[2])))
	{
		mexErrMsgTxt("Valid input size is: 768x128xN (packed), 768x768xN, or 768x1024xN, all UINT8");
		return;
	}

	unsigned char *Input = (unsigned char *)mxGetData(prhs[2]);
	if (alps[devID] != nullptr)
	{

		if (dim1[1] == 1024 || dim1[1] == 768)
		{
			unsigned char *Pat = alps[devID]->packInput(Input, dim1[1], dim1[0], numFrames);
			int allocatedSequenceID = alps[devID]->uploadSequence(Pat, numFrames);
			plhs[0] = mxCreateDoubleScalar(allocatedSequenceID);
			delete Pat;
		}
		else
		{
			// packed input. no need to repack
			int allocatedSequenceID = alps[devID]->uploadSequence(Input, numFrames);
			plhs[0] = mxCreateDoubleScalar(allocatedSequenceID);
		}
	}
	else
	{
		plhs[0] = mxCreateDoubleScalar(-1);
	}
}


void ReleaseSequence(int devID, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	if (nrhs != 3)
	{
		mexErrMsgTxt("Use: ALPwrapper('DevID, ReleaseSequence',SequenceID'\n");
		plhs[0] = mxCreateDoubleScalar(false);
		return;
	}
	
	int seqID = (int)(*(double *)mxGetData(prhs[2]));
	bool retValue = alps[devID]->releaseSequence(seqID);
	plhs[0] = mxCreateDoubleScalar(retValue);
}
 
void PlaySequence(int devID, int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
	if (nrhs != 5)
	{
		mexErrMsgTxt("Use: ALPwrapper('PlaySequence',DevID, SequenceID, FrameRate(Hz), NumRepeats (0=continuous)'\n");
		plhs[0] = mxCreateDoubleScalar(false);
		return;
	}

	int seqID= (int) (*(double *)mxGetData(prhs[2]));
	double frameRateHz = *(double *)mxGetData(prhs[3]);
	long numRepeats = (long)(*(double *)mxGetData(prhs[4]));
	bool retValue;

	if (numRepeats == 0)
		retValue = alps[devID]->runUploadedSequence(seqID, frameRateHz, true);
	else
		retValue = alps[devID]->runUploadedSequence(seqID, frameRateHz, false, numRepeats);

	plhs[0] = mxCreateDoubleScalar(retValue);
}


void exitFunction()
{
	if (alps != nullptr)
	{
		for (int k = 0; k < MAX_CONNECTED_DMDS; k++)
		{
			if (alps[k] != nullptr)
				delete alps[k];
		}
	}
}


void mexFunction(int nlhs, mxArray *plhs[],
	int nrhs, const mxArray *prhs[]) {

	ALP_ID nAlpId1, nAlpId2;
	int Res1 = AlpDevAlloc(0, ALP_DEFAULT, &nAlpId1);
	int Res2 = AlpDevAlloc(1, ALP_DEFAULT, &nAlpId2);
	return;


	int StringLength = int(mxGetNumberOfElements(prhs[0])) + 1;
	char* Command = new char[StringLength];
	if (mxGetString(prhs[0], Command, StringLength) != 0){
		mexErrMsgTxt("\nError extracting the command.\n");
		return;
	}
	else if (strcmp(Command, "Init") == 0) {
		if (nrhs != 2)
		{
			mexErrMsgTxt("\nError. Please specify ALP device index.\n");
			return;
		}
		int devID = (int)(*(double *)mxGetData(prhs[1]));
		if (devID < 0 || devID >= MAX_CONNECTED_DMDS)
		{
			mexErrMsgTxt("\nError. ALP device index is out of range.\n");
			return;
		}
		mexAtExit(exitFunction);

		if (alps[devID] != nullptr)
			delete alps[devID];

		alps[devID] = new ALPwrapper(devID);
		bool Success = alps[devID]->init();
		plhs[0] = mxCreateDoubleScalar(Success);
	}
	else if (strcmp(Command, "IsInitialized") == 0) {
		if (nrhs != 2)
		{
			mexErrMsgTxt("\nError. Please specify ALP device index.\n");
			return;
		}
		int devID = (int)(*(double *)mxGetData(prhs[1]));
		if (devID < 0 || devID >= MAX_CONNECTED_DMDS)
		{
			mexErrMsgTxt("\nError. ALP device index is out of range.\n");
			return;
		}
		if (alps[devID] == nullptr)
			plhs[0] = mxCreateDoubleScalar(false);
		else
			plhs[0] = mxCreateDoubleScalar(alps[devID]->isInitialized());
	}
	else
	{
		if (nrhs < 2)
		{
			mexErrMsgTxt("\nError. Please specify ALP device index.\n");
			return;
		}
		int devID = (int)(*(double *)mxGetData(prhs[1]));

		if (alps[devID] == nullptr)
		{
			mexPrintf("Cannot execute command. Not initialized.\n");
			delete Command;
			return;
		}

		if (strcmp(Command, "ClearWhite") == 0) {
			alps[devID]->clear(true);
		}
		else if (strcmp(Command, "ClearBlack") == 0) {
			alps[devID]->clear(false);
		}
		else if (strcmp(Command, "ShowPattern") == 0) {
			ShowPattern(devID,nlhs, plhs, nrhs, prhs);
		}
		else if (strcmp(Command, "StopSequence") == 0) {
			alps[devID]->stopSequence();
		}
		else if (strcmp(Command, "WaitForSequenceCompletion") == 0) {
			alps[devID]->waitForSequenceCompletion();
		}
		else if (strcmp(Command, "UploadPatternSequence") == 0) {
			UploadPatternSequence(devID,nlhs, plhs, nrhs, prhs);
		}
		else if (strcmp(Command, "PlayUploadedSequence") == 0) {
			PlaySequence(devID, nlhs, plhs, nrhs, prhs);
		}
		else if (strcmp(Command, "ReleaseSequence") == 0) {
			ReleaseSequence(devID, nlhs, plhs, nrhs, prhs);
		}
		else if (strcmp(Command, "ReleaseAllSequences") == 0) {
			alps[devID]->releaseAllSequences();
		}
		else if (strcmp(Command, "Release") == 0) {
			delete alps[devID];
			alps[devID] = nullptr;
			mexPrintf("ALP handles released.\n");
		}
		else if (strcmp(Command, "HasSequenceCompleted") == 0)
		{
			plhs[0] = mxCreateLogicalScalar(alps[devID]->hasSequenceCompleted());
		}
		else {
			mexPrintf("Error. Unknown command\n");
		}
	}
	delete Command;

}

