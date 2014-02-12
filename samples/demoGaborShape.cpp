#include <iostream>
#include <opencv2/highgui/highgui.hpp>
#include "filters.hpp"
#include "transforms.hpp"
#include "descriptors.hpp"
#include "utils.hpp"

using namespace std;
using namespace cv;

/// Set the number of patches
int mhor = 5, mver = 7, nhor = 6, nver = 2;
/// Establish the number of bins
int histSize = 8;
/// Set the ranges of histogram
float range[] = {0, 1} ;
const float* histRange = { range };

bool PCALDA = true;

Mat extractGaborShape(InputArray _img){
	
	Mat img = _img.getMat();
	vector<vector<Mat> > mpatches, rpatches;
	int count = 0;
	Mat hist, radon, gaborMag;
	Mat temp = Mat::zeros(1, 40*mhor*mver*nhor*nver*histSize, CV_32F);
	
	for(int mu=0; mu<8; mu++){
		for(int nu=0; nu<5; nu++){
			gaborMag = magnitude(convolveDFT(img, gaborWavelet(mu, nu, 2*CV_PI, 0)));
			patcher(gaborMag, Size(gaborMag.cols/mhor, gaborMag.rows/mver), 0, mpatches);
			for(uint mcol=0; mcol<mpatches.size(); mcol++){
				for(uint mrow=0; mrow<mpatches[mcol].size(); mrow++){
					radon = radonTransform(mpatches[mcol][mrow]);
					patcher(radon, Size(radon.cols/nhor, radon.rows/nver), 0, rpatches);
					for(uint rcol=0; rcol<rpatches.size(); rcol++){
						for(uint rrow=0; rrow<rpatches[rcol].size(); rrow++){
							calcHist(&rpatches[rcol][rrow], 1, 0, Mat(), hist, 1, &histSize, &histRange);
							for(int pos=0; pos<histSize; pos++){
								temp.at<float>(count*histSize+pos) = hist.at<float>(pos);
							}
							count++;
						}
					}
					rpatches.clear();
					vector<vector<Mat> >().swap(rpatches);
				}
			}
			mpatches.clear();
			vector<vector<Mat> >().swap(mpatches);
		}
	}
	return temp;
}


int main(int argc, char** argv)
{
	
	vector<string> trainingPhotos, trainingSketches, testingPhotos, testingSketches, extraPhotos, vphotos, vsketches;
	
	loadImages(argv[1], vphotos);
	loadImages(argv[2], vsketches);
	//loadImages(argv[3], testingPhotos);
	//loadImages(argv[4], testingSketches);
	//loadImages(argv[5], extraPhotos);
	
	
	if(PCALDA){
		trainingPhotos.insert(trainingPhotos.end(),vphotos.begin()+694,vphotos.begin()+1194);
		trainingSketches.insert(trainingSketches.end(),vsketches.begin()+694,vsketches.begin()+1194);
		
		if(trainingPhotos.size()!=trainingSketches.size()){
			cerr << "Training photos and sketches sets has different sizes" << endl;
			return -1;
		}
	}
	
	testingPhotos.insert(testingPhotos.end(),vphotos.begin(),vphotos.begin()+694);
	testingSketches.insert(testingSketches.end(),vsketches.begin(),vsketches.begin()+694);
	
	uint nTraining = (uint)trainingPhotos.size();
	cout << "The size of training set is: " << nTraining << endl;
	
	vector<Mat*> testingPhotosGaborShape, testingSketchesGaborShape, extraPhotosGaborShape;
	
	Mat trainingData(2*nTraining, 40*mhor*mver*nhor*nver*histSize, CV_32F);
	vector<int> labels(2*nTraining);
	
	Mat img, xi, temp, mean, eigenvectors;
	PCA pca;
	LDA lda;
	
	if(PCALDA){
		#pragma omp parallel for private(img, xi, temp)
		for(uint i=0; i<nTraining; i++){
			img = imread(trainingPhotos[i],0);
			resize(img, img, Size(128,160));
			cout << "trainingPhoto " << i << endl;
			xi = trainingData.row(i);
			temp = extractGaborShape(img);
			normalize(temp, temp, 1, 0, NORM_MINMAX);
			temp.copyTo(xi);		
			labels[i]=i;
		}
		
		#pragma omp parallel for private(img, xi, temp)
		for(uint i=0; i<nTraining; i++){
			img = imread(trainingSketches[i],0);
			resize(img, img, Size(128,160));
			cout << "trainingSketches " << i << endl;
			xi = trainingData.row(nTraining+i);
			temp = extractGaborShape(img);
			normalize(temp, temp, 1, 0, NORM_MINMAX);
			temp.copyTo(xi);
			labels[nTraining+i]=i;
		}
		
		mean = Mat::zeros(1, trainingData.cols, CV_32F);
		
		// calculate sums
		for (int i = 0; i < trainingData.rows; i++) {
			Mat instance = trainingData.row(i);
			add(mean, instance, mean);
		}
		
		// calculate total mean
		mean.convertTo(mean, CV_32F, 1.0/static_cast<double>(trainingData.rows));
		
		// subtract the mean of matrix
		for(int i=0; i<trainingData.rows; i++) {
			Mat r_i = trainingData.row(i);
			subtract(r_i, mean.reshape(1,1), r_i);
		}
		
		cout << "Starting the training" << endl;
		
		int dim = 700;
		
		pca(trainingData, Mat(), CV_PCA_DATA_AS_ROW, dim);
		
		Mat trainingDataPCA = pca.project(trainingData);
		
		lda.compute(trainingDataPCA, labels);
		
		Mat ldaEigenvectors;
		lda.eigenvectors().convertTo(ldaEigenvectors, CV_32F);
		
		gemm(pca.eigenvectors, ldaEigenvectors, 1.0, Mat(), 0.0, eigenvectors, GEMM_1_T);
		
		cout << "Finish the training" << endl;
	}
	
	uint nTestingSketches = testingSketches.size();
	uint nTestingPhotos = testingPhotos.size();
	
	testingSketchesGaborShape.resize(nTestingSketches); 
	testingPhotosGaborShape.resize(nTestingPhotos); 
	
	cout << "The number of subjects is: " << nTestingSketches << endl;
	cout << "The number of photos in gallery is: "<< nTestingPhotos << endl;
	
	#pragma omp parallel for private(img, temp)
	for(uint i=0; i<nTestingPhotos; i++){
		img = imread(testingPhotos[i],0);
		resize(img, img, Size(128,160));
		cout << "testingPhotos " << i << endl;
		testingPhotosGaborShape[i] = new Mat();
		if(PCALDA){
			temp = extractGaborShape(img);
			normalize(temp, temp, 1, 0, NORM_MINMAX);
			gemm((temp-mean),eigenvectors, 1.0, Mat(), 0.0, temp);
		}
		else{
			temp = extractGaborShape(img);
		}
		//normalize(temp, temp, 1, 0, NORM_MINMAX);
		*(testingPhotosGaborShape[i]) = temp;
		//cout << i<<" "<<*(testingPhotosGaborShape[i]) << endl;
	}
	
	#pragma omp parallel for private(img, temp)
	for(uint i=0; i<nTestingSketches; i++){
		img = imread(testingSketches[i],0);
		resize(img, img, Size(128,160));
		cout << "testingSketches " << i << endl;
		testingSketchesGaborShape[i] = new Mat();
		if(PCALDA){
			temp = extractGaborShape(img);
			normalize(temp, temp, 1, 0, NORM_MINMAX);
			gemm((temp-mean),eigenvectors, 1.0, Mat(), 0.0, temp);
		}
		else{
			temp = extractGaborShape(img);
		}
		//normalize(temp, temp, 1, 0, NORM_MINMAX);
		*(testingSketchesGaborShape[i]) = temp;
		//cout << i <<" "<<*(testingSketchesGaborShape[i]) << endl;
	}
	
	for(uint i=0; i<nTestingSketches; i++){
		cout <<*(testingPhotosGaborShape[i]) << endl;
		cout <<*(testingSketchesGaborShape[i]) << endl;
	}
	
	cerr << "calculating distances" << endl;
	
	Mat distances = Mat::zeros(nTestingSketches,nTestingPhotos,CV_64F);
	FileStorage file("gs-cufsf-lda.xml", FileStorage::WRITE);
	
	#pragma omp parallel for
	for(uint i=0; i<nTestingSketches; i++){
		for(uint j=0; j<nTestingPhotos; j++){
			distances.at<double>(i,j) = norm(*(testingPhotosGaborShape[j]),*(testingSketchesGaborShape[i]));//chiSquareDistance();
		}
	}
	
	file << "distanceMatrix" << distances;
	file.release();
	
	return 0;
}

