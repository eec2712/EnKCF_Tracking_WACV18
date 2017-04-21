#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "kcftracker.hpp"
#include "Filter_Definition.h"
#include <vector>
#include <math.h>
#include <string>
#include <opencv2/core/core.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "opencv2/imgcodecs/imgcodecs.hpp"
#include "opencv2/videoio/videoio.hpp"
#include <unistd.h>
#include <stack>
#include <random>
#include "../main/include/interface.h"
#include "../tools/include/image_tools.h"
#include "../edge/include/edge_boxes_interface.h"

using namespace cv;
using namespace std;

std::stack<clock_t> tictoc_stack;

///
/// tic and toc is used to measure the time to process an operation
///
void tic() {
    tictoc_stack.push(clock());
}

// TOC
void toc() {
    std::cout << "Time elapsed: "
              << ((double)(clock() - tictoc_stack.top())) / CLOCKS_PER_SEC
              << std::endl;
    tictoc_stack.pop();
}

void help()
{
    cout
    << "\n----------------------------------------------------------------------------\n"
    << "USAGE:\n"
    << "./KCF -d <MOV image filename> |-g <ground truth filename> | -o <output file> | -s <save Video file> | -e <save estimation file> \n"
    << "\nEXAMPLE:\n"                                                                     
    << "./KCF -d DJI_1.MOV -g Ground_Truth1.txt -o output1.txt -e estimation.txt -s DJI_1_Output.wmv\n"
    << "----------------------------------------------------------------------------\n"
    << endl;
}


// Define the Bounding Box Parameters
float xMin, yMin, width, height;
float xMinScale,yMinScale;
cv::Point pos;
int var1=1;
///
/// An Interactive ROI Drawal Tool
///
void CallBackFunc(int event, int x, int y, int flags, void* userdata)
{
/* Need to be able to control both up and down event for the ROI */
    switch( event )
    {
    case EVENT_MOUSEMOVE:
        break;
    case EVENT_LBUTTONDOWN:
        xMin = x;
        yMin = y;
        break;
    case EVENT_LBUTTONUP:
        pos.x = x;
        pos.y = y;
        width = x - xMin;
        height =  y - yMin;
        cout << "xMin = " << xMin << " yMin = " << yMin << "  width = " << width << "  height = " << height << endl;
        break;
    }
}


int main(int argc, char* argv[])
{

   // Define Parameters for the Kernelized Correlation Filter Tracker - Options
   int opt, verbose = 0;
   bool HOG = true;  	      // Enable HoG Features
   bool FIXEDWINDOW = false;  // Fixed Window
   bool MULTISCALE = true;    // Scale Space Search Enabled
   bool SILENT = false;	      // Suppress the Outputs
   bool LAB = true;	      // Enable or Disable Color Features
   char szDataFile[256];
   char gtDataFile[256];
   char szImageFile[256];
   char szSaveVideofile[256];
   char path_model[256] = "/home/buzkent/EdgeBox/model_train.txt"; 
   char prDataFile[256];   

   // Terminate if less than 9 inputs are provided
   if (argc < 9) {
      help();
      return -2;
   }

   while ((opt = getopt(argc,argv,"e:d:g:p:")) != EOF) {
      switch(opt)
      {
         case 'e': memset(szImageFile, '\0', sizeof(szImageFile));
            strcpy(szImageFile, optarg);
            cout <<" Input MOV Data File: "<< optarg <<endl;
            break;
         case 'd': memset(szDataFile, '\0', sizeof(szDataFile));
            strcpy(szDataFile, optarg); 
            cout <<" Input MOV Data File: "<< optarg <<endl; 
            break;
         case 'g': memset(gtDataFile, '\0', sizeof(gtDataFile));
            strcpy(gtDataFile, optarg);
            cout <<" Input MOV Data File: "<< optarg <<endl;
            break;
        case 'p': memset(prDataFile, '\0', sizeof(prDataFile));
            strcpy(prDataFile, optarg);
            cout <<" Input MOV Data File: "<< optarg <<endl;
      }
   }

   ///
   /// Create KCFTracker Object
   /// \param[in] HOG : Flag to Enable or Disable Hog Features
   /// \param[in] FIXEDWINDOW : Flag to Enable or Disable Fixed Window Implementation
   /// \param[in] MULTISCALE : Flag to Include Scale Space Search
   /// \param[in] HOG : Flag to Include Color Features
   ///
   KCFTracker tracker(HOG, FIXEDWINDOW, MULTISCALE, LAB);

   ///
   /// Create Particle Filter Tracker Object
   /// \param[in] N_Particles : Number of Particles in the Particle Filter
   /// \param[in] dimension : The State Space Dimension
   /// \param[in] beta :  Weight Function Parameter
   /// \param[in] Q : Transition Noise Variance
   /// \param[in] R : Measurement Noise Variance
   ///
   int N_Particles = 1000;     // Number of Particles
   int dimension = 4;          // State Space Dimensionality
   vector<double> Q{10,10,5,5}; // Transition Noise Variance
   double R = 5;               // Measurement Noise Variance
   double beta = 0.10;         // Likelihood Parameter
   Particle_Filter Tracker(N_Particles,dimension,beta,Q,R);

   // Frame read
   cv::Mat frame, frame_old;

   // Tracker results
   cv::Rect result;

   // Read Tracker-by-Detection Output and Ground Truth
   std::vector<double> Obs {0,0,0,0};
   std::vector<double> State_Mean {0,0,0,0};

   // Video to Overlay Bounding Boxes on the Frames
   cv::VideoWriter outvid;
   int codec = cv::VideoWriter::fourcc('W', 'M','V', '2');  // select desired codec (must be available at runtime)
   std::vector<std::vector<double> > RMSE(2);

   // Generate Random Numbers used in the Particle Filter
   int seed2 = time(0);
   std::default_random_engine engine2(seed2);
   std::uniform_int_distribution<int> dist2(0,1000);
   float rdThresholds[3] = {4.00,2.50,1.00};
   int rdIndex = 0, rdTriggerIndex = 0; // Indexes for ReDetection
   // Read the Video
   cv::VideoCapture capt;
   if (strncmp(argv[2],"video",5) == 0){
      string path = szDataFile;
      capt.open(path);
   }
   int nFrames = 0;
   int fontFace = cv::FONT_HERSHEY_SCRIPT_SIMPLEX;
   int thickness = 2;
   cv::namedWindow("Name", WINDOW_NORMAL);
   cv::resizeWindow("Name", 800,800);
   
   // Read the Ground Truth Text File
char ch;
#ifdef _VOT15_DATASET_
   float g1X, g1Y, g2X, g2Y, g3X, g3Y, g4X, g4Y;
#endif

#ifdef _UAV123_DATASET_
   int gX, gY, gWidth, gHeight;
   int fFrame,lFrame,frameID;
   std::string seqName; 
   std::string seqNameID;
   std::string matchString = prDataFile;
   std::ifstream startFrame("/home/buzkent/Desktop/startFrames_UAV123.txt");
   // Matched Frame
   while(startFrame >> fFrame >> lFrame >> seqNameID >> seqName){
     if (seqNameID.compare(matchString) == 0){
         frameID = fFrame;
         break;
      }	
   }
#endif

   // Read the Ground Truth File for the Object of Interest
   std::ifstream groundTruth(gtDataFile);
   std::cout << gtDataFile << std::endl;

   // Declare A Vector for Euclidean Distance
   std::vector<std::vector<float>> Metrics(2);
   int cfIndex = 0;

#ifdef _USE_VIDEO_INPUT__
   while (1) // Read the Next Frame
   {
#endif

#ifdef _UAV123_DATASET_
   int skipOPE = 0;
   while(groundTruth >> gX >> ch >> gY >> ch >> gWidth >> ch >> gHeight)
   {
       if(gX < -100){
          skipOPE = 1;
       }
       else{
          skipOPE = 0;
       }
#endif
      
#ifdef _VOT15_DATASET_
   while(groundTruth >> g1X >> ch >> g1Y >> ch >> g2X >> ch >> g2Y >> ch >> g3X >> ch >> g3Y >> ch >> g4X >> ch >> g4Y)
   {
      float xVertices[4] = {g1X,g2X,g3X,g4X};
      float yVertices[4] = {g1Y,g2Y,g3Y,g4Y};
      cv::Mat xCoord(2,2, CV_32F, xVertices);
      cv::Mat yCoord(2,2, CV_32F, yVertices);
      double minValue, maxValue;
      cv::Point minLoc, maxLoc;
      cv::Rect boundRect;
      cv::minMaxLoc(xCoord, &minValue, &maxValue, &minLoc, &maxLoc);
      boundRect.x = minValue; boundRect.width = maxValue - minValue;
      cv::minMaxLoc(yCoord, &minValue, &maxValue, &minLoc, &maxLoc);
      boundRect.y = minValue; boundRect.height = maxValue - minValue;
#endif
      // Read the Frame to Perform Tracking
      std::stringstream ss;
#ifdef _UAV123_DATASET_     
      ss << std::setfill('0') << std::setw(6) << frameID;
#endif
#ifdef _VOT15_DATASET_     
      ss << std::setfill('0') << std::setw(8) << nFrames+1;
#endif
      if (strncmp(argv[2],"video",5) == 0){
         capt >> frame;  // Read Next Frame
         cv::resize(frame,frame,cv::Size(1280,720)); // Resize it to Make it Same with H2 Implementation     
      }
      else{
	frame = cv::imread(szDataFile+ss.str()+".jpg");
      }
      using std::default_random_engine; // Initiate the random device at each step
      using std::uniform_int_distribution; // Random Number Generator

      ///
      /// PERFORM TRACKING
      ///
      if (nFrames == 0) 
      {
        var1 = 0;
        /// Observation on the first frame given by the user
        // Obs[0] = xMin+width/2; Obs[1] = yMin+height/2; Obs[2] = width; Obs[3] = height;
#ifdef _UAV123_DATASET_
        Obs[0] = gX; Obs[1] = gY; Obs[2] = gWidth; Obs[3] = gHeight;
#endif

#ifdef _VOT15_DATASET_
        Obs[0] = boundRect.x; Obs[1] = boundRect.y; Obs[2] = boundRect.width; Obs[3] = boundRect.height;
#endif
        ///
        /// Initiate the Kernelized Correlation Filter Trackers - Translation and Scale Filters
        /// \param[in] Rect : Rectangle Object for the Bounding Box
        /// \param[in] frame : The First Frame
        ///
        tracker.init( Rect(Obs[0], Obs[1], Obs[2], Obs[3]), frame );
        cv::rectangle(frame,cv::Point(Obs[0],Obs[1]),cv::Point(Obs[0]+Obs[2],Obs[1]+Obs[3]),cv::Scalar(0,255,0),4,8);
        ///
        /// Initiate the Particle of the Particle Filter
        /// \param[in] Obs : Observation given by the user in the first frame
        ///
        // Tracker.particle_initiation(Obs);
      }
      else {
        /// -------------------- UPDATE STEP --------------------------------------------
        /// Use Translation and Scale Filter Interchangeably
        tracker.PSR_scale = 20;
        tracker.PSR_wroi  = 20;
        tracker.PSR_sroi = 20;
        float PSR;
        cv::Rect roi_WROI = tracker.extracted_w_roi;
        if ( (cfIndex == 0) | (cfIndex == 2) | (cfIndex == 3)){
           // Apply Homography to the Track Position at Previous Frame
           // tracker._roi_w = tracker.applyHomography(homography, frame, tracker._roi_w);
           result = tracker.updateWROI(frame);  // Estimate Translation using Wider ROI
           PSR = tracker.PSR_wroi;              // Tracker Confidence
        }
        if ( (cfIndex == 4) ){
          // Apply Homography to the Track Position at Previous Frame
          // tracker._roi = tracker.applyHomography(homography, frame, tracker._roi);
          result = tracker.updateScale(frame);      // Estimate Translation
          PSR = tracker.PSR_scale;
        }
        if ( cfIndex == 1){
          result = tracker.updateScale(frame); // Estimate Scale
          PSR = tracker.PSR_scale;    
        }
        std::cout << "Filter Type" << cfIndex << std::endl;
        cfIndex++;
        if (cfIndex > 4){
 	    cfIndex = 0;
        }

        /// -------------------------------------------------------------------------------
        /// Trigger the Redetection if needed
	if ((tracker.PSR_wroi < rdThresholds[0]) || (tracker.PSR_sroi < rdThresholds[1]) || (tracker.PSR_scale < rdThresholds[2]) || (isnan(PSR) == 1)){
         
	   // Determine the ROI for Re-Detection
           cv::Mat roiImage = frame.clone();
           std::vector<cv::Mat> src;
           cv::split(roiImage,src);

	   // Increament the Number of Times redetection is called			
	   rdTriggerIndex++;

           /// Fill in the Image Data Structure Parameters1
           autel::computer_vision::AuMat image;
           image.cols_ = roiImage.cols;
           image.rows_ = roiImage.rows;
           image.depth_ = 0;
           image.dims_ = 2;
           image.channels_ = roiImage.channels();
           image.step_[1] = image.channels_;
           image.step_[0] = image.cols_ * image.step_[1];
           unsigned char* memory_image = (unsigned char*)malloc(roiImage.cols*roiImage.rows*roiImage.channels());
           image.data_ = memory_image;
           for (int i = 0; i < roiImage.rows ; i++){
              for(int j = 0; j < roiImage.cols ; j++){
                 image.data_[(i*roiImage.cols+j) * roiImage.channels()] = src[0].at<uchar>(i,j);
                 image.data_[(i*roiImage.cols+j) * roiImage.channels()+1] = src[1].at<uchar>(i,j);
                 image.data_[(i*roiImage.cols+j) * roiImage.channels()+2] = src[2].at<uchar>(i,j);
             }
           }

           // Initialize edge box
           InitializedBox(path_model);
           std::vector<cv::Vec4i> BoundingBoxes;
           BoundingBoxes = EdgeBoxInterface(image);

           // Perform Re-detection - Re-detection Interface
           std::vector<pair<int,float>> boxProposed;
           tic();
           std::pair<int,float> MatchedBoxIndex = tracker.target_redetection(BoundingBoxes, frame, result, rdTriggerIndex, boxProposed);
           toc();

           // Display the Considered Boxes on the Frame
           for (int i = 0; i < boxProposed.size(); i++){
               int j = boxProposed[i].first;
               std::string rdConf = to_string(float(boxProposed[i].second));
               cv::rectangle(roiImage,cv::Point(BoundingBoxes[j][0],BoundingBoxes[j][1]),
               cv::Point(BoundingBoxes[j][0]+BoundingBoxes[j][2],BoundingBoxes[j][1]+BoundingBoxes[j][3]),cv::Scalar(0,255,0),4,8);
               cv::putText(roiImage,rdConf, cv::Point(BoundingBoxes[j][0],(BoundingBoxes[j][1])), fontFace, 4, cv::Scalar::all(255), 2, 4);  
           }
 
           // Display the ROI Searched by the Large Translation Filter
           cv::rectangle(roiImage,cv::Point(roi_WROI.x,roi_WROI.y),cv::Point(roi_WROI.x+roi_WROI.width,roi_WROI.y+roi_WROI.height),cv::Scalar(255,0,0),4,8);

           // Crop the Re-detection ROI
           cv::imshow("RD ROI",roiImage);
           cv::waitKey(0);

           // Re-initiate the Tracker If the Confidence is High Enough
           Obs[0] = BoundingBoxes[MatchedBoxIndex.first][0]; Obs[1] = BoundingBoxes[MatchedBoxIndex.first][1]; 
           Obs[2] = BoundingBoxes[MatchedBoxIndex.first][2]; Obs[3] = BoundingBoxes[MatchedBoxIndex.first][3]; 
           if (MatchedBoxIndex.second > 2.00){
              // Re-Initiate the Track
              tracker.init( Rect(Obs[0],Obs[1],Obs[2],Obs[3]), frame );
              result.x = Obs[0]; result.y = Obs[1]; result.width = Obs[2]; result.height = Obs[3];
              rdTriggerIndex = -1;
           }
           // if the object is not found, initiate with the object with highest objectness
           if(MatchedBoxIndex.second < 2.00 && rdTriggerIndex >= 2.0){
              // Re-Initiate the Track
              tracker.init( Rect(Obs[0],Obs[1],Obs[2],Obs[3]), frame );
              result.x = Obs[0]; result.y = Obs[1]; result.width = Obs[2]; result.height = Obs[3];
              rdTriggerIndex = -1;
           }

           // Draw the Selected Rectangle
           std::string rd_confidence = to_string(float(MatchedBoxIndex.second));
           cv::rectangle(frame,cv::Point(Obs[0],Obs[1]),cv::Point(Obs[0]+Obs[2],Obs[1]+Obs[3]),cv::Scalar(0,255,0),4,8);
           cv::putText(frame,rd_confidence, cv::Point(Obs[0],Obs[1]), fontFace, 4, cv::Scalar::all(255), thickness, 4); // Display the Confidence
        }
       
        // TRACKING RESULTS OVERLAID ON THE FRAME
        cv::rectangle( frame, cv::Point(result.x,result.y), cv::Point(result.x+result.width,result.y+result.height), cv::Scalar(255,0,0),4,8);
        std::string confidence = to_string(int(PSR));
        cv::putText(frame,confidence, cv::Point(result.x-result.width/2,result.y-result.height/2), fontFace, 4, cv::Scalar::all(255), thickness, 4);

	// Compute the Euclidean Distance for Precision Metric
        if (skipOPE != 1){
#ifdef _UAV123_DATASET_
           float eucDistance = pow(pow(((float) gX + (float) gWidth/2.0) - ((float) result.x + (float) result.width/2.0),2) + pow(((float) gY + (float) gHeight/2.0) - ((float) result.y + (float) result.height/2.0),2),0.5);
           cv:Rect gtRect(gX,gY,gWidth,gHeight);
#endif
#ifdef _VOT15_DATASET_
        float eucDistance = pow(pow((boundRect.x + boundRect.width/2.0) - (result.x + result.width/2),2) + pow((boundRect.y+boundRect.height/2.0) - (result.y + result.height/2.0),2),0.5);
#endif
        // Store the Euclidean Distance to the Ground Truth
        Metrics[0].push_back(eucDistance);

#ifdef _VOT15_DATASET_
        cv:Rect gtRect(g1X,g1Y,g4X,g4Y);
#endif
        cv::Rect tRect(result.x,result.y,result.width,result.height);
        cv::Rect Inters = gtRect & tRect;
        cv::Rect Un = gtRect |  tRect;
        float sucOverlap= 100.00 * (float) Inters.area() / (float) Un.area();
        Metrics[1].push_back(sucOverlap);

        }      
      }
      
      /// Save the Frame into the Output Video
      if (strncmp(argv[2],"video",5) == 0){
         cv::resize(frame,frame,cv::Size(800,800));
      }
      // outvid.write(frame);
      // cv::imshow("Name", frame);
      nFrames++;
      frameID++;
      if (frameID > lFrame){
         break;
      }
      if (!SILENT) {
         cv::imshow("Name", frame);
         cv::waitKey(0);
      }
   }

   // Estimate Precision Curve
   string performanceFile = prDataFile;
   PrecisionCurve(Metrics, prDataFile);
}
