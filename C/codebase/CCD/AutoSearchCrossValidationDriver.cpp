/*
 * AutoSearchCrossValidationDriver.cpp
 *
 *  Created on: Dec 10, 2013
 *      Author: msuchard
 */


// TODO Change from fixed grid to adaptive approach in BBR

#include <iostream>
#include <iomanip>
#include <numeric>
#include <math.h>
#include <cstdlib>

#include "AutoSearchCrossValidationDriver.h"
#include "CyclicCoordinateDescent.h"
#include "CrossValidationSelector.h"
#include "AbstractSelector.h"
#include "ccd.h"
#include "../utils/HParSearch.h"

namespace bsccs {

const static int MAX_STEPS = 50;

AutoSearchCrossValidationDriver::AutoSearchCrossValidationDriver(
			const ModelData& _modelData,
			int iGridSize,
			double iLowerLimit,
			double iUpperLimit,
			vector<real>* wtsExclude) : modelData(_modelData), maxPoint(0), gridSize(iGridSize),
			lowerLimit(iLowerLimit), upperLimit(iUpperLimit), weightsExclude(wtsExclude),
			maxSteps(MAX_STEPS) {

	// Do anything???
}

AutoSearchCrossValidationDriver::~AutoSearchCrossValidationDriver() {
	// Do nothing
}

double AutoSearchCrossValidationDriver::computeGridPoint(int step) {
	if (gridSize == 1) {
		return upperLimit;
	}
	// Log uniform grid
	double stepSize = (log(upperLimit) - log(lowerLimit)) / (gridSize - 1);
	return exp(log(lowerLimit) + step * stepSize);
}

void AutoSearchCrossValidationDriver::logResults(const CCDArguments& arguments) {

	ofstream outLog(arguments.cvFileName.c_str());
	if (!outLog) {
		cerr << "Unable to open log file: " << arguments.cvFileName << endl;
		exit(-1);
	}
	outLog << std::scientific << maxPoint << std::endl;
	outLog.close();
}

void AutoSearchCrossValidationDriver::resetForOptimal(
		CyclicCoordinateDescent& ccd,
		CrossValidationSelector& selector,
		const CCDArguments& arguments) {

	ccd.setWeights(NULL);
	ccd.setHyperprior(maxPoint);
	ccd.resetBeta(); // Cold-start
}

void AutoSearchCrossValidationDriver::hierarchyDrive(
		CyclicCoordinateDescent& ccd,
		AbstractSelector& selector,
		const CCDArguments& arguments) {

	// TODO Check that selector is type of CrossValidationSelector

	std::vector<real> weights;

	double tryvalue = modelData.getNormalBasedDefaultVar();
	double tryvalueClass = tryvalue; // start with same variance at the class and element level; // for hierarchy class variance
	UniModalSearch searcher(10, 0.01, log(1.5));
	UniModalSearch searcherClass(10, 0.01, log(1.5)); // Need a better way to do this.

	const double eps = 0.05; //search stopper
	std::cout << "Default var = " << tryvalue << std::endl;


	bool finished = false;
	bool drugLevelFinished = false;
	bool classLevelFinished = false;

	int step = 0;
	while (!finished) {

		// More hierarchy logic
		ccd.setHyperprior(tryvalue);
		ccd.setClassHyperprior(tryvalueClass);

		/* start code duplication */
		std::vector<double> predLogLikelihood;
		for (int i = 0; i < arguments.foldToCompute; i++) {
			int fold = i % arguments.fold;
			if (fold == 0) {
				selector.permute(); // Permute every full cross-validation rep
			}

			// Get this fold and update
			selector.getWeights(fold, weights);
			if(weightsExclude){
				for(int j = 0; j < (int)weightsExclude->size(); j++){
					if(weightsExclude->at(j) == 1.0){
						weights[j] = 0.0;
					}
				}
			}
			ccd.setWeights(&weights[0]);
			std::cout << "Running at " << ccd.getPriorInfo() << " ";
			ccd.update(arguments.maxIterations, arguments.convergenceType, arguments.tolerance);

			// Compute predictive loglikelihood for this fold
			selector.getComplement(weights);
			if(weightsExclude){
				for(int j = 0; j < (int)weightsExclude->size(); j++){
					if(weightsExclude->at(j) == 1.0){
						weights[j] = 0.0;
					}
				}
			}

			double logLikelihood = ccd.getPredictiveLogLikelihood(&weights[0]);

			std::cout << "Grid-point #" << (step + 1) << " at " << tryvalue;
			std::cout << "\tFold #" << (fold + 1)
			          << " Rep #" << (i / arguments.fold + 1) << " pred log like = "
			          << logLikelihood << std::endl;

			// Store value
			predLogLikelihood.push_back(logLikelihood);
		}

		double pointEstimate = computePointEstimate(predLogLikelihood);
		/* end code duplication */

		double stdDevEstimate = computeStDev(predLogLikelihood, pointEstimate);

		std::cout << "AvgPred = " << pointEstimate << " with stdev = " << stdDevEstimate << std::endl;
        //searcher.tried(tryvalue, pointEstimate, stdDevEstimate);
        //pair<bool,double> next = searcher.step();

		// Hierarchy logic

        // alternate adapting the class and element level, unless one is finished
        if ((step % 2 == 0 && !drugLevelFinished) || classLevelFinished){
        	searcher.tried(tryvalue, pointEstimate, stdDevEstimate);
        	pair<bool,double> next = searcher.step();
        	tryvalue = next.second;
            std::cout << "Next point at " << next.second << " and " << next.first << std::endl;
            if (!next.first) {
               	drugLevelFinished = true;
            }
       	} else {
       		searcherClass.tried(tryvalueClass, pointEstimate, stdDevEstimate);
       		pair<bool,double> next = searcherClass.step();
       		tryvalueClass = next.second;
       	    std::cout << "Next Class point at " << next.second << " and " << next.first << std::endl;
            if (!next.first) {
               	classLevelFinished = true;
            }
        }
        // if everything is finished, end.
        if (drugLevelFinished && classLevelFinished){
        	finished = true;
        }


        std::cout << searcher;
        step++;
        if (step >= maxSteps) {
        	std::cerr << "Max steps reached!" << std::endl;
        	finished = true;
        }
	}

	maxPoint = tryvalue;

	// Report results
	std::cout << std::endl;
	std::cout << "Maximum predicted log likelihood estimated at:" << std::endl;
	std::cout << "\t" << maxPoint << " (variance)" << std::endl;
	std::cout << "class level = " << tryvalueClass << endl;


	if (!arguments.useNormalPrior) {
		double lambda = convertVarianceToHyperparameter(maxPoint);
		std::cout << "\t" << lambda << " (lambda)" << std::endl;
	}
	std:cout << std::endl;
}

void AutoSearchCrossValidationDriver::drive(
		CyclicCoordinateDescent& ccd,
		AbstractSelector& selector,
		const CCDArguments& arguments) {

	// TODO Check that selector is type of CrossValidationSelector

}

} // namespace
