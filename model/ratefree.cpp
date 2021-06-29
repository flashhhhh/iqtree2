/*
 * ratefree.cpp
 *
 *  Created on: Nov 3, 2014
 *      Author: minh
 */

#include "tree/phylotree.h"
#include "ratefree.h"
#include "rateinvar.h"

#include "model/modelfactory.h"
#include "model/modelmixture.h"
#include "variablebounds.h"

#include <utils/stringfunctions.h> //for convert_double_vec
#include <utils/timeutil.h> //temporary : for time log-lining

const double MIN_FREE_RATE = 0.001;
const double MAX_FREE_RATE = 1000.0;
const double TOL_FREE_RATE = 0.0001;

// Modified by Thomas on 13 May 2015
//const double MIN_FREE_RATE_PROP = 0.0001;
//const double MAX_FREE_RATE_PROP = 0.9999;
const double MIN_FREE_RATE_PROP = 0.001;
const double MAX_FREE_RATE_PROP = 1000;

RateFree::RateFree(int ncat, PhyloTree *tree, PhyloTree* report_to_tree)
    : RateGamma(ncat, tree, report_to_tree) {
	fix_params           = 0;
	prop                 = nullptr;
    sorted_rates         = false;
    optimizing_params    = 0;
    optimize_alg         = report_to_tree->params->optimize_alg_freerate;
    proportion_tolerance = 1e-4;
    rate_tolerance       = 1e-4;
	setNCategory(ncat);
}

RateFree::RateFree(int ncat, double start_alpha, string params,
                   bool use_sorted_rates, string opt_alg, PhyloTree *tree)
    : RateGamma(ncat, start_alpha, false, tree) {
	fix_params           = 0;
	prop                 = nullptr;
    sorted_rates         = use_sorted_rates;
    optimizing_params    = 0;
    optimize_alg         = opt_alg;
    proportion_tolerance = 1e-4;
    rate_tolerance       = 1e-4;

	setNCategory(ncat);

	if (params.empty()) {
        return;
    }
	DoubleVector params_vec;
	try {
		convert_double_vec(params.c_str(), params_vec);
		int    i;
		double sum, sum_prop;
        if (params_vec.size() == ncategory) {
            // only inputing prop
            for (i = 0, sum_prop = 0.0; i < ncategory; i++) {
                prop[i] = params_vec[i];
                rates[i] = 1.0;
                sum_prop += prop[i];
            }
            fix_params = (Params::getInstance().optimize_from_given_params) ? 0 : 1;
        } else {
            if (params_vec.size() != ncategory*2) {
                outError("Number of parameters for FreeRate model"
                         " must be twice the number of categories");
            }
            for (i = 0, sum = 0.0, sum_prop = 0.0; i < ncategory; i++) {
                prop[i] = params_vec[i*2];
                rates[i] = params_vec[i*2+1];
                sum += prop[i]*rates[i];
                sum_prop += prop[i];
            }
            for (i = 0; i < ncategory; i++) {
                rates[i] /= sum;
            }
            fix_params = (Params::getInstance().optimize_from_given_params) ? 0 : 2;
        }
		if (fabs(sum_prop-1.0) > 1e-5) {
			outError("Sum of category proportions not equal to 1");
        }
	} catch (string &str) {
		outError(str);
	}
}

void RateFree::startCheckpoint() {
    checkpoint->startStruct("RateFree" + convertIntToString(ncategory));
}

void RateFree::saveCheckpoint() {
    startCheckpoint();
    CKP_ARRAY_SAVE(ncategory, prop);
    CKP_ARRAY_SAVE(ncategory, rates);
    endCheckpoint();
}

void RateFree::restoreCheckpoint() {
    startCheckpoint();
    CKP_ARRAY_RESTORE(ncategory, prop);
    CKP_ARRAY_RESTORE(ncategory, rates);
    endCheckpoint();
}

void RateFree::setNCategory(int ncat) {

    // initialize with gamma rates
    super::setNCategory(ncat);
    delete [] prop;
	prop  = new double[ncategory];
    for (int i = 0; i < ncategory; i++) {
        prop[i] = (1.0-getPInvar())/ncategory;
    }
	name = "+R";
	name += convertIntToString(ncategory);
	full_name = "FreeRate";
	full_name += " with " + convertIntToString(ncategory) + " categories";
}

void RateFree::initFromCatMinusOne() {
    ncategory--;
    restoreCheckpoint();
    ncategory++;

    int first = 0;
    // get the category k with largest proportion
    for (int i = 1; i < ncategory-1; i++) {
        if (prop[i] > prop[first]) {
            first = i;
        }
    }
    int second = (first == 0) ? 1 : 0;
    for (int i = 0; i < ncategory-1; i++) {
        if (prop[i] > prop[second] && i != first) {            
            second = i;
        }
    }

    // divide highest category into 2 of the same prop
    // 2018-06-12: fix bug negative rates
    if (-rates[second] + 3*rates[first] > 0.0) {
        rates[ncategory-1] = (-rates[second] + 3*rates[first])/2.0;
        rates[first] = (rates[second]+rates[first])/2.0;
    } else {
        rates[ncategory-1] = (3*rates[first])/2.0;
        rates[first] = (rates[first])/2.0;
    }
    prop[ncategory-1] = prop[first]/2;
    prop[first] = prop[first]/2;
    sortUpdatedRates();

    phylo_tree->clearAllPartialLH();
}

RateFree::~RateFree() {
    delete [] prop;
    prop = nullptr;
}

void RateFree::sortUpdatedRates() {
    // sort the rates in increasing order
    if (sorted_rates) {
        quicksort(rates, 0, ncategory-1, prop);
    }
}

std::string RateFree::getNameParams() const {
	stringstream str;
    const char* separator="";
	str << "+R" << ncategory << "{";
	for (int i = 0; i < ncategory; i++) {
		str << separator << prop[i]<< "," << rates[i];
        separator = ",";
	}
	str << "}";
	return str.str();
}

double RateFree::meanRates() const {
	double ret = 0.0;
	for (int i = 0; i < ncategory; i++) {
		ret += prop[i] * rates[i];
    }
	return ret;
}

/**
 * rescale rates s.t. mean rate is equal to 1, useful for FreeRate model
 * @return rescaling factor
 */
double RateFree::rescaleRates() {
	double norm = meanRates();
	for (int i = 0; i < ncategory; i++) {
		rates[i] /= norm;
    }
	return norm;
}

int RateFree::getNDim() const { 
    if (fix_params == 2) {
        return 0;
    }
    if (fix_params == 1) { 
        // only fix prop
        return (ncategory-1);
    }
    if (optimizing_params == 0) {
        return (2*ncategory-2); 
    }
    if (optimizing_params == 1) { 
        // rates
        return ncategory-1;
    }
    if (optimizing_params == 2) { 
        // proportions
        return ncategory-1;
    }
    return 0;
}

const std::string& RateFree::getOptimizationAlgorithm() const {
    return optimize_alg;
}

void RateFree::setGammaShape(double shape) { 
    gamma_shape = shape; 
}

void RateFree::setFixProportions(bool fixed) {
    fix_params = fixed ? 2 : 0;
}

void RateFree::setFixRates(bool fixed) {
    fix_params = fixed ? 1 : 0;
}

void RateFree::setOptimizationAlgorithm(const std::string& algorithm) { 
    optimize_alg = algorithm; 
}

bool RateFree::isOptimizingProportions() const {
    return optimizing_params != 1;
}

bool RateFree::isOptimizingRates() const {
    return optimizing_params != 2;
}

bool RateFree::isOptimizingShapes() const {
    return false;
}

bool RateFree::areProportionsFixed() const {
    return fix_params == 1;
}

double RateFree::targetFunk(double x[]) {
	getVariables(x);
    if (isOptimizingRates()) {
        // only clear partial_lh if optimizing rates
        phylo_tree->clearAllPartialLH();
    }
	return -phylo_tree->computeLikelihood();
}

/**
	optimize parameters. Default is to optimize gamma shape
	@return the best likelihood
*/
double RateFree::optimizeParameters(double gradient_epsilon,
                                    PhyloTree* report_to_tree) {
	int ndim = getNDim();
    if (ndim == 0) {
        // return if nothing to be optimized
        return phylo_tree->computeLikelihood();
    }
    TREE_LOG_LINE(*report_to_tree, VerboseMode::VB_MED,
                  "Optimizing " << name << " model parameters by "
                  << optimize_alg << " algorithm...");
    // TODO: turn off EM algorithm for +ASC model
    if ((optimize_alg.find("EM") != string::npos
         && phylo_tree->getModelFactory()->unobserved_ptns.empty())) {
        if (fix_params == 0) {
            return optimizeWithEM(report_to_tree);
        }
    }

    VariableBounds vb(ndim+1);
	double score;

//    score = optimizeWeights();

    int left = 1, right = 2;
    if (areProportionsFixed()) {
        // fix proportions
        right = 1;
    }
    if (optimize_alg.find("1-BFGS") != string::npos) {
        left  = 0; 
        right = 0;
    }

    // changed to Wi -> Ri by Thomas on Sept 11, 15
    for (optimizing_params = right; optimizing_params >= left; optimizing_params--) {
        ndim = getNDim();
        // by BFGS algorithm
        setVariables(vb.variables);
        setBounds(vb.lower_bound, vb.upper_bound, vb.bound_check);

        if (optimize_alg.find("BFGS-B") != string::npos) {
            score = -L_BFGS_B(ndim, vb.variables+1, 
                              vb.lower_bound+1, vb.upper_bound+1,
                              max(gradient_epsilon, TOL_FREE_RATE));
        }
        else {
            score = -minimizeMultiDimen(vb.variables, ndim, 
                                        vb.lower_bound, vb.upper_bound,
                                        vb.bound_check, max(gradient_epsilon, TOL_FREE_RATE));
        }
        getVariables(vb.variables);
        sortUpdatedRates();
        phylo_tree->clearAllPartialLH();
        score = phylo_tree->computeLikelihood();
    }
    optimizing_params = 0;
	return score;
}

void RateFree::setBounds(double *lower_bound, double *upper_bound,
                         bool *bound_check) {
	if (getNDim() == 0) {
        return;
    }
	int i;
    if (optimizing_params == 2) {
        // proportions
        for (i = 1; i < ncategory; i++) {
            lower_bound[i] = MIN_FREE_RATE_PROP;
            upper_bound[i] = MAX_FREE_RATE_PROP;
            bound_check[i] = false;
        }
    } else if (optimizing_params == 1){
        // rates
        for (i = 1; i < ncategory; i++) {
            lower_bound[i] = MIN_FREE_RATE;
            upper_bound[i] = MAX_FREE_RATE;
            bound_check[i] = false;
        }
    } else {
        // both weights and rates
        for (i = 1; i < ncategory; i++) {
            lower_bound[i] = MIN_FREE_RATE_PROP;
            upper_bound[i] = MAX_FREE_RATE_PROP;
            bound_check[i] = false;
        }
        for (i = 1; i < ncategory; i++) {
            lower_bound[i+ncategory-1] = MIN_FREE_RATE;
            upper_bound[i+ncategory-1] = MAX_FREE_RATE;
            bound_check[i+ncategory-1] = false;
        }
    }
}

void RateFree::setVariables(double *variables) {
	if (getNDim() == 0) return;
	int i;

	// Modified by Thomas on 13 May 2015
	// --start--
	/*
	variables[1] = prop[0];
	for (i = 2; i < ncategory; i++)
		variables[i] = variables[i-1] + prop[i-1];
	*/
    
    if (optimizing_params == 2) {    
        // proportions
        for (i = 0; i < ncategory-1; i++)
            variables[i+1] = prop[i] / prop[ncategory-1];
    } else if (optimizing_params == 1) {
        // rates
        for (i = 0; i < ncategory-1; i++)
            variables[i+1] = rates[i];
    } else {
        // both rates and weights
        for (i = 0; i < ncategory-1; i++) {
            variables[i+1] = prop[i] / prop[ncategory-1];
        }
        for (i = 0; i < ncategory-1; i++) {
            variables[i+ncategory] = rates[i] / rates[ncategory-1];
        }
    }
}

bool RateFree::getVariables(double *variables) {
	if (getNDim() == 0) return false;
	int i;
    bool changed = false;
	// Modified by Thomas on 13 May 2015
	// --start--
	/*
	double *y = new double[2*ncategory+1];
	double *z = y+ncategory+1;
	//  site proportions: y[0..c] <-> (0.0, variables[1..c-1], 1.0)
	y[0] = 0; y[ncategory] = 1.0;
	memcpy(y+1, variables+1, (ncategory-1) * sizeof(double));
	std::sort(y+1, y+ncategory);

	// category rates: z[0..c-1] <-> (variables[c..2*c-2], 1.0)
	memcpy(z, variables+ncategory, (ncategory-1) * sizeof(double));
	z[ncategory-1] = 1.0;
	//std::sort(z, z+ncategory-1);

	double sum = 0.0;
	for (i = 0; i < ncategory; i++) {
		prop[i] = (y[i+1]-y[i]);
		sum += prop[i] * z[i];
	}
	for (i = 0; i < ncategory; i++) {
		rates[i] = z[i] / sum;
	}

	delete [] y;
	*/

	double sum = 1.0;
    if (optimizing_params == 2) {
        // proportions
        for (i = 0; i < ncategory-1; i++) {
            sum += variables[i+1];
        }
        for (i = 0; i < ncategory-1; i++) {
            changed |= (prop[i] != variables[i+1] / sum);
            prop[i] = variables[i+1] / sum;
        }
        changed |= (prop[ncategory-1] != 1.0 / sum);
        prop[ncategory-1] = 1.0 / sum;
        // added by Thomas on Sept 10, 15
        // update the values of rates, in order to
        // maintain the sum of prop[i]*rates[i] = 1
//        sum = 0;
//        for (i = 0; i < ncategory; i++) {
//            sum += prop[i] * rates[i];
//        }
//        for (i = 0; i < ncategory; i++) {
//            rates[i] = rates[i] / sum;
//        }
    } else if (optimizing_params == 1) {
        // rates
        for (i = 0; i < ncategory-1; i++) {
            changed |= (rates[i] != variables[i+1]);
            rates[i] = variables[i+1];
        }
        // added by Thomas on Sept 10, 15
        // need to normalize the values of rates, in order to
        // maintain the sum of prop[i]*rates[i] = 1
//        sum = 0;
//        for (i = 0; i < ncategory; i++) {
//            sum += prop[i] * rates[i];
//        }
//        for (i = 0; i < ncategory; i++) {
//            rates[i] = rates[i] / sum;
//        }
    } else {
        // both weights and rates
        for (i = 0; i < ncategory-1; i++) {
            sum += variables[i+1];
        }
        for (i = 0; i < ncategory-1; i++) {
            changed |= (prop[i] != variables[i+1] / sum);
            prop[i] = variables[i+1] / sum;
        }
        changed |= (prop[ncategory-1] != 1.0 / sum);
        prop[ncategory-1] = 1.0 / sum;
        
        // then rates
    	sum = prop[ncategory-1];
    	for (i = 0; i < ncategory-1; i++) {
    		sum += prop[i] * variables[i+ncategory];
    	}
    	for (i = 0; i < ncategory-1; i++) {
            changed |= (rates[i] != variables[i+ncategory] / sum);
    		rates[i] = variables[i+ncategory] / sum;
    	}
        changed |= (rates[ncategory-1] != 1.0 / sum);
    	rates[ncategory-1] = 1.0 / sum;
    }
	// --end--
    return changed;
}

/**
	write information
	@param out output stream
*/
void RateFree::writeInfo(ostream &out) {
	out << "Site proportion and rates: ";
	for (int i = 0; i < ncategory; i++)
		out << " (" << prop[i] << "," << rates[i] << ")";
	out << endl;
}

/**
	write parameters, used with modeltest
	@param out output stream
*/
void RateFree::writeParameters(ostream &out) {
	for (int i = 0; i < ncategory; i++)
		out << "\t" << prop[i] << "\t" << rates[i];

}

namespace {
    const double MIN_PROP = 1e-4;
};

double RateFree::optimizeWithEM(PhyloTree* report_to_tree) {
    intptr_t   nptn     = phylo_tree->aln->getNPattern();
    size_t     nmix     = ncategory;
    double*    new_prop = aligned_alloc<double>(nmix);
    PhyloTree* tree     = new PhyloTree;

    tree->copyPhyloTree(phylo_tree, true);
    tree->optimize_by_newton = phylo_tree->optimize_by_newton;
    tree->setParams(phylo_tree->params);
    tree->setLikelihoodKernel(phylo_tree->sse);
    tree->setNumThreads(phylo_tree->num_threads);

    // initialize model
    ModelFactory *model_fac   = new ModelFactory();
    model_fac->joint_optimize = phylo_tree->params->optimize_model_rate_joint;
//    model_fac->unobserved_ptns = phylo_tree->getModelFactory()->unobserved_ptns;

    RateHeterogeneity *site_rate = new RateHeterogeneity; 
    tree->setRate(site_rate);
    site_rate->setTree(tree);
            
    model_fac->site_rate = site_rate;
    tree->model_factory  = model_fac;
    tree->setParams(phylo_tree->params);
    double old_score = 0.0;
    // EM algorithm loop described in Wang, Li, Susko, and Roger (2008)
    for (int step = 0; step < ncategory; step++) {
        // first compute _pattern_lh_cat
        double score = phylo_tree->computePatternLhCat(WSL_RATECAT);
        TREE_LOG_LINE(*phylo_tree, VerboseMode::VB_DEBUG, 
                      "At start of EM step " << step
                      << " likelihood score is " << score);
        if (score > 0.0) {
            phylo_tree->printTree(cout, WT_BR_LEN+WT_NEWLINE);
            writeInfo(cout);
        }
        ASSERT(score < 0);
        
        if (step > 0) {
            if (score <= old_score-0.1) {
                phylo_tree->printTree(cout, WT_BR_LEN+WT_NEWLINE);
                writeInfo(cout);
                TREE_LOG_LINE(*phylo_tree, VerboseMode::VB_QUIET,
                    "Partition " << phylo_tree->aln->name << "\n"
                    << "score: " << score << "  old_score: " << old_score);
                if (!Params::getInstance().ignore_any_errors) {
                    ASSERT(score > old_score - 0.1);
                }
            }
        }
        old_score = score;
        
        doEStep(nptn, new_prop, nmix);
        int  maxpropid = doMStep(new_prop, nmix);
        bool zero_prop = regularizeProportions(new_prop, nmix, maxpropid);
        // break if some probabilities too small
        if (zero_prop) {
            break;
        }

        bool converged = true;
        double sum_prop = 0.0;
        double new_pinvar = 0.0;
        for (size_t c = 0; c < nmix; c++) {
            // check for convergence
            sum_prop   += new_prop[c];
            converged   = converged && (fabs(prop[c]-new_prop[c]) < proportion_tolerance);
            prop[c]     = new_prop[c];
            new_pinvar += new_prop[c];
        }

        new_pinvar = 1.0 - new_pinvar;
        if (new_pinvar > 1e-4 && getPInvar() != 0.0) {
            converged = converged && (fabs(getPInvar()-new_pinvar) < proportion_tolerance);
            if (isFixPInvar()) {
                outError("Fixed given p-invar is not supported");
            }
            setPInvar(new_pinvar);
            phylo_tree->computePtnInvar();
        }
        
        ASSERT(fabs(sum_prop+new_pinvar-1.0) < MIN_PROP);
        
        optimizeRatesOneByOne(tree, nptn, converged, model_fac);

        phylo_tree->clearAllPartialLH();
        if (converged) break;
    }
    
    sortUpdatedRates();
    
    delete tree;
    aligned_free(new_prop);
    return phylo_tree->computeLikelihood();
}

void RateFree::doEStep(intptr_t nptn, double* new_prop, size_t nmix) {
    // E-step
    // decoupled weights (prop) from _pattern_lh_cat
    // to obtain L_ci and compute pattern likelihood L_i
    memset(new_prop, 0, nmix*sizeof(double));
    for (intptr_t ptn = 0; ptn < nptn; ptn++) {
        double *this_lk_cat = phylo_tree->tree_buffers._pattern_lh_cat + ptn*nmix;
        double lk_ptn = phylo_tree->ptn_invar[ptn];
        for (size_t c = 0; c < nmix; c++) {
            lk_ptn += this_lk_cat[c];
        }
        ASSERT(lk_ptn != 0.0);
        lk_ptn = phylo_tree->ptn_freq[ptn] / lk_ptn;
        
        // transform _pattern_lh_cat into posterior probabilities of each category
        for (size_t c = 0; c < nmix; c++) {
            this_lk_cat[c] *= lk_ptn;
            new_prop[c]    += this_lk_cat[c];
        }
    }
}

int RateFree::doMStep(double* new_prop, size_t nmix) {
    // M-step, update weights according to (*)
    int    maxpropid  = 0;
    double reciprocalOfNSite = 1.0 / (double)(phylo_tree->getAlnNSite());
    for (int c = 0; c < nmix; c++) {
        new_prop[c] *= reciprocalOfNSite;
        if (new_prop[c] > new_prop[maxpropid]) {
            maxpropid = c;
        }
    }
    return maxpropid;
}

bool RateFree::regularizeProportions(double* new_prop, size_t nmix, 
                           size_t  maxpropid) {
    // regularize prop
    bool zero_prop = false;
    for (size_t c = 0; c < nmix; c++) {
        if (new_prop[c] < MIN_PROP) {
            new_prop[maxpropid] -= (MIN_PROP - new_prop[c]);
            new_prop[c]          = MIN_PROP;
            zero_prop            = true;
        }
    }
    return zero_prop;
}

void RateFree::optimizeRatesOneByOne(PhyloTree*    tree,  
                                     intptr_t      nptn, 
                                     bool&         converged,
                                     ModelFactory* model_fac) {
    size_t nmix = ncategory;
    double sum = 0.0;

    for (int c = 0; c < nmix; c++) {
        tree->copyPhyloTree(phylo_tree, true);
        ModelMarkov *subst_model;
        if (phylo_tree->getModel()->isMixture()
            && phylo_tree->getModelFactory()->fused_mix_rate) {
            auto model  = phylo_tree->getModel();
            subst_model = (ModelMarkov*)model->getMixtureClass(c);
        }
        else {
            subst_model = (ModelMarkov*)phylo_tree->getModel();
        }
        tree->setModel(subst_model);
        subst_model->setTree(tree);
        model_fac->model = subst_model;
        if (subst_model->isMixture()
            || subst_model->isSiteSpecificModel()
            || !subst_model->isReversible()) {
            tree->setLikelihoodKernel(phylo_tree->sse);
        }

        // initialize likelihood
        tree->initializeAllPartialLh();
        // copy posterior probability into ptn_freq
        tree->computePtnFreq();
        double *this_lk_cat = phylo_tree->tree_buffers._pattern_lh_cat+c;
        for (intptr_t ptn = 0; ptn < nptn; ptn++) {
            tree->ptn_freq[ptn] = this_lk_cat[ptn*nmix];
        }
        double scaling = rates[c];
        tree->scaleLength(scaling);
        tree->optimizeTreeLengthScaling(MIN_PROP, scaling, 1.0/prop[c], 0.001);
        converged = converged && (fabs(rates[c] - scaling) < rate_tolerance);
        rates[c] = scaling;
        sum += prop[c] * rates[c];
        // reset subst model
        tree->setModel(nullptr);
        subst_model->setTree(phylo_tree);
    }
}

void RateFree::setProportionTolerance(double tol) {
    ASSERT(0<tol);
    proportion_tolerance = tol;
}

void RateFree::setRateTolerance(double tol) {
    ASSERT(0<tol);
    rate_tolerance = tol;
}