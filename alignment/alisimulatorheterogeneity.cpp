//
//  alisimulatorheterogeneity.cpp
//  model
//
//  Created by Nhan Ly-Trong on 23/03/2021.
//

#include "alisimulatorheterogeneity.h"

AliSimulatorHeterogeneity::AliSimulatorHeterogeneity(Params *params) :
AliSimulator(params) {
    rate_heterogeneity = tree->getRate();
}

AliSimulatorHeterogeneity::AliSimulatorHeterogeneity(AliSimulator *alisimulator){
    tree = alisimulator->tree;
    params = alisimulator->params;
    num_sites_per_state = alisimulator->num_sites_per_state;
    length_ratio = alisimulator->length_ratio;
    expected_num_sites = alisimulator->expected_num_sites;
    partition_rate = alisimulator->partition_rate;
    rate_heterogeneity = tree->getRate();
    max_length_taxa_name = alisimulator->max_length_taxa_name;
    fundi_items = alisimulator->fundi_items;
}

/**
    initialize site specific model index based on its weights in the mixture model
*/
void AliSimulatorHeterogeneity::intializeSiteSpecificModelIndex()
{
    int sequence_length = expected_num_sites;
    site_specific_model_index.resize(sequence_length);
    
    // if a mixture model is used -> randomly select a model for each site based on the weights of model components
    if (tree->getModel()->isMixture())
    {
        // get/init variables
        ModelSubst* model = tree->getModel();
        int num_models = model->getNMixtures();
        double *model_prop = new double[num_models];
        
        // get the weights of model components
        bool isFused = model->isFused();
        int max_prob_pos = 0;
        for (int i = 0; i < num_models; i++)
        {
            // fused model, take the weight from site_rate
            if (isFused)
                model_prop[i] = tree->getRate()->getProp(i) / (1.0 - tree->getRate()->getPInvar());
            else
                model_prop[i] = model->getMixtureWeight(i);
            
            // finding the max probability position
            if (model_prop[i] > model_prop[max_prob_pos])
                max_prob_pos = i;
        }
            
        // convert the model_prop into an accumulated model_prop
        convertProMatrixIntoAccumulatedProMatrix(model_prop, 1, num_models);
        
        for (int i = 0; i < sequence_length; i++)
        {
            // randomly select a model from the set of model components, considering its probability array.
            site_specific_model_index[i] = getRandomItemWithAccumulatedProbMatrixMaxProbFirst(model_prop, 0, num_models, max_prob_pos);
        }
        
        // delete the probability array of rate categories
        delete[] model_prop;
        
        // regenerate ancestral sequence based on mixture model component base fequencies
        regenerateAncestralSequenceMixtureModel();
    }
    // otherwise, if it's not a mixture model -> set model index = 0 for all sites
    else
    {
        // set model index = 0 for all sites
        for (int i = 0; i < sequence_length; i++)
        {
            site_specific_model_index[i] = 0;
        }
    }
}

/**
    regenerate ancestral sequence based on mixture model component base fequencies
*/
void AliSimulatorHeterogeneity::regenerateAncestralSequenceMixtureModel(){
    // only regenerate the ancestral sequence if mixture model is used and the ancestral sequence is not specified by the user.
    if (tree->getModel()->isMixture() && !tree->params->alisim_ancestral_sequence_aln_filepath){
        // dummy variables
        ModelSubst* model = tree->getModel();
        int num_models = model->getNMixtures();
        int num_states = tree->aln->getMaxNumStates();
        
        // initialize base frequencies maxtrix
        double * base_freqs_all_components = new double[num_models*num_states];
        double * base_freqs_one_component = new double[num_states];
        
        // retrieve base frequencies of each model component
        for (int i = 0; i < num_models; i++)
        {
            model->getStateFrequency(base_freqs_one_component, i);
            
            // copy base_freqs_one_component to base_freqs_all_components
            for (int j = 0; j < num_states; j++)
                base_freqs_all_components[i*num_states+j] = base_freqs_one_component[j];
        }
        
        // delete base_freqs_one_component
        delete [] base_freqs_one_component;
        
        // convert base_freqs_all_components to accummulated matrix
        convertProMatrixIntoAccumulatedProMatrix(base_freqs_all_components, num_models, num_states);
        
        // re-generate the ancestral sequence
        for (int i = 0; i < expected_num_sites; i++)
        {
            double rand_num = random_double();
            int starting_index = site_specific_model_index[i]*num_states;
            tree->root->sequence[i] = binarysearchItemWithAccumulatedProbabilityMatrix(base_freqs_all_components, rand_num, starting_index, starting_index + num_states - 1, starting_index) - starting_index;
        }
        
        // delete base_freqs_one_component
        delete [] base_freqs_all_components;
    }
}

/**
    initialize state freqs for all model components (of a mixture model)
*/
void AliSimulatorHeterogeneity::intializeStateFreqsMixtureModel()
{
    // get/init variables
    ModelSubst* model = tree->getModel();
    
    // only initialize state freqs if it's a mixture model && the state freqs have not been estimated by an inference process yet
    if (model->isMixture() && tree->aln->aln_file.length() == 0 && model->getFreqType() == FREQ_EMPIRICAL)
    {
        // get max_num_bases
        int max_num_states = tree->aln->getMaxNumStates();
        
        // initialize state freqs
        double *state_freq = new double[max_num_states];
        
        // get the weights of model components
        for (int i = 0; i < model->getNMixtures(); i++)
            if (model->getMixtureClass(i)->getFreqType() == FREQ_EMPIRICAL)
            {
                generateRandomBaseFrequencies(state_freq, max_num_states);
                model->getMixtureClass(i)->setStateFrequency(state_freq);
            }
        
        // delete state_freq
        delete [] state_freq;
    }
}

/**
    initialize caching accumulated_trans_matrix
*/
void AliSimulatorHeterogeneity::intializeCachingAccumulatedTransMatrices(double *cache_trans_matrix, int num_models, int num_rate_categories, int max_num_states, DoubleVector branch_lengths, double *trans_matrix, ModelSubst* model)
{
    bool fuse_mixture_model = (model->isMixture() && model->isFused());
    
    // initialize the cache_trans_matrix
    for (int model_index = 0; model_index < num_models; model_index++)
        for (int category_index = 0; category_index < num_rate_categories; category_index++)
        {
            // skip computing unused trans_matrices if a mixture with fused site rate is used
            if (fuse_mixture_model && model_index != category_index)
                continue;
            
            double rate = rate_heterogeneity->getNRate() == 1?1:rate_heterogeneity->getRate(category_index);
            double branch_length_by_category = rate_heterogeneity->isHeterotachy()?branch_lengths[category_index]:branch_lengths[0];
            
            // compute the transition matrix
            model->computeTransMatrix(partition_rate*branch_length_by_category*rate, trans_matrix, model_index);
            
            // copy the transition matrix to the cache_trans_matrix
            for (int trans_index = 0; trans_index < max_num_states*max_num_states; trans_index++)
            {
                int cache_index = model_index*num_rate_categories*max_num_states*max_num_states + category_index*max_num_states*max_num_states + trans_index;
                cache_trans_matrix[cache_index] = trans_matrix[trans_index];
            }
        }
    
    // convert cache_trans_matrix into an accumulated cache_trans_matrix
    convertProMatrixIntoAccumulatedProMatrix(cache_trans_matrix, num_models*num_rate_categories*max_num_states, max_num_states);
}

/**
*  simulate sequences for all nodes in the tree by DFS
*
*/
void AliSimulatorHeterogeneity::simulateSeqs(int sequence_length, double *site_specific_rates, ModelSubst *model, double *trans_matrix, int max_num_states, Node *node, Node *dad, ostream &out, vector<string> state_mapping)
{
    // process its neighbors/children
    NeighborVec::iterator it;
    FOR_NEIGHBOR(node, dad, it) {
        // reset the num_children_done_simulation
        if (node->num_children_done_simulation >= (node->neighbors.size() - 1))
            node->num_children_done_simulation = 0;
        
        // estimate the sequence for the current neighbor
        // check if trans_matrix could be caching (without rate_heterogeneity or the num of rate_categories is lowr than the threshold (5)) or not
        if (tree->getRateName().empty()
            || (!tree->getModelFactory()->is_continuous_gamma && rate_heterogeneity && rate_heterogeneity->getNDiscreteRate() <= params->alisim_max_rate_categories_for_applying_caching))
        {
            int num_models = tree->getModel()->isMixture()?tree->getModel()->getNMixtures():1;
            int num_rate_categories  = tree->getRateName().empty()?1:rate_heterogeneity->getNDiscreteRate();
            double *cache_trans_matrix = new double[num_models*num_rate_categories*max_num_states*max_num_states];
            
            // initialize a set of branch_lengths
            DoubleVector branch_lengths;
            branch_lengths.resize(num_rate_categories);
            for (int i = 0; i < num_rate_categories; i++)
                branch_lengths[i] = (*it)->getLength(i);
            
            // initialize caching accumulated trans_matrices
            intializeCachingAccumulatedTransMatrices(cache_trans_matrix, num_models, num_rate_categories, max_num_states, branch_lengths, trans_matrix, model);

            // estimate the sequence
            (*it)->node->sequence.resize(sequence_length);
            for (int i = 0; i < sequence_length; i++)
                (*it)->node->sequence[i] = estimateStateFromAccumulatedTransMatrices(cache_trans_matrix, site_specific_rates[i] , i, num_rate_categories, max_num_states, node->sequence[i]);
            
            // delete cache_trans_matrix
            delete [] cache_trans_matrix;
        }
        // otherwise, estimating the sequence without trans_matrix caching
        else
        {
            (*it)->node->sequence.resize(sequence_length);
            for (int i = 0; i < sequence_length; i++)
               // randomly select the state, considering it's dad states, and the transition_probability_matrix
                (*it)->node->sequence[i] = estimateStateFromOriginalTransMatrix(model, site_specific_model_index[i], site_specific_rates[i], trans_matrix, max_num_states, (*it)->length, node->sequence[i]);
        }
        
        // permuting selected sites for FunDi model
        if (params->alisim_fundi_taxon_set.size()>0)
        {
            if (node->isLeaf())
                permuteSelectedSites(fundi_items, node);
            if ((*it)->node->isLeaf())
                permuteSelectedSites(fundi_items, (*it)->node);
        }
        
        // writing and deleting simulated sequence immediately if possible
        writeAndDeleteSequenceImmediatelyIfPossible(out, state_mapping, it, node);
        
        // browse 1-step deeper to the neighbor node
        simulateSeqs(sequence_length, site_specific_rates, model, trans_matrix, max_num_states, (*it)->node, node, out, state_mapping);
    }
}

/**
  estimate the state from accumulated trans_matrices
*/
int AliSimulatorHeterogeneity::estimateStateFromAccumulatedTransMatrices(double *cache_trans_matrix, double site_specific_rate, int site_index, int num_rate_categories, int max_num_states, int dad_state)
{
    // randomly select the state, considering it's dad states, and the accumulated trans_matrices
    int model_index = site_specific_model_index[site_index];
    int category_index = site_specific_rate_index[site_index];
    int starting_index = model_index*num_rate_categories*max_num_states*max_num_states + category_index*max_num_states*max_num_states + max_num_states*dad_state;
    
    ASSERT(category_index > RATE_ZERO_INDEX);
    
    return getRandomItemWithAccumulatedProbMatrixMaxProbFirst(cache_trans_matrix, starting_index, max_num_states, dad_state);
}

/**
  estimate the state from an original trans_matrix
*/
int AliSimulatorHeterogeneity::estimateStateFromOriginalTransMatrix(ModelSubst *model, int model_component_index, double rate, double *trans_matrix, int max_num_states, double branch_length, int dad_state)
{    
    // compute the transition matrix
    model->computeTransMatrix(partition_rate*branch_length*rate, trans_matrix, model_component_index);
    
    // iteratively select the state, considering it's dad states, and the transition_probability_matrix
    int starting_index = dad_state*max_num_states;
    return getRandomItemWithProbabilityMatrix(trans_matrix, starting_index, max_num_states);
}

/**
    get site-specific rates based on Continuous Gamma Distribution
*/
void AliSimulatorHeterogeneity::getSiteSpecificRatesContinuousGamma(double *site_specific_rates, int sequence_length)
{
    RateContinuousGamma *rate_continuous_gamma = new RateContinuousGamma(rate_heterogeneity->getGammaShape(), params->ran_seed);
    
    rate_continuous_gamma->getSiteSpecificRates(site_specific_rates, sequence_length);
    
    // delete rate_continuous_gamma
    delete rate_continuous_gamma;
}

/**
    get site-specific rates based on Discrete Distribution (Gamma/FreeRate)
*/
void AliSimulatorHeterogeneity::getSiteSpecificRatesDiscrete(double *site_specific_rates, int sequence_length)
{
    int num_rate_categories = rate_heterogeneity->getNDiscreteRate();
    
    // initialize the probability array of rate categories
    double *category_probability_matrix = new double[num_rate_categories];
    int max_prob_pos = 0;
    for (int i = 0; i < num_rate_categories; i++)
    {
        category_probability_matrix[i] = rate_heterogeneity->getProp(i);
        
        // finding the max probability position
        if (category_probability_matrix[i] > category_probability_matrix[max_prob_pos])
            max_prob_pos = i;
    }
    
    // convert the probability matrix of rate categories into an accumulated probability matrix of rate categories
    convertProMatrixIntoAccumulatedProMatrix(category_probability_matrix, 1, num_rate_categories);
    
    // initialize the site-specific rates
    for (int i = 0; i < sequence_length; i++)
    {
        // randomly select a rate from the set of rate categories, considering its probability array.
        int rate_category = getRandomItemWithAccumulatedProbMatrixMaxProbFirst(category_probability_matrix, 0, num_rate_categories, max_prob_pos);
        
        // if rate_category == -1 <=> this site is invariant -> return dad's state
        if (rate_category == -1)
        {
            site_specific_rates[i] = 0;
            site_specific_rate_index[i] = RATE_ZERO_INDEX;
        }
        else // otherwise, get the rate of that rate_category
        {
            site_specific_rates[i] = rate_heterogeneity->getRate(rate_category);
            site_specific_rate_index[i] = rate_category;
        }
    }
    
    // delete the probability array of rate categories
    delete[] category_probability_matrix;
}

/**
    get site-specific rates
*/
void AliSimulatorHeterogeneity::getSiteSpecificRates(double *site_specific_rates, int sequence_length)
{
    site_specific_rate_index.resize(sequence_length);
    
    // if a mixture model is supplied and it's fused with site rates -> set site_specific_rate_index equals to site_specific_model_index
    if (tree->getModel()->isMixture() && tree->getModel()->isFused())
    {
        // get invariant_probability
        double invariant_prop = tree->getRate()->getPInvar();

        for (int i = 0; i < sequence_length; i++)
        {
            // handle invariant sites
            if (random_double() <= invariant_prop)
            {
                site_specific_rate_index[i] = RATE_ZERO_INDEX;
                site_specific_rates[i] = 0;
            }
            // or set the rate index equal to the model index
            else
            {
                site_specific_rate_index[i] = site_specific_model_index[i];
                site_specific_rates[i] = rate_heterogeneity->getRate(site_specific_rate_index[i]);
            }
        }
        return;
    }
    
    string rate_name = tree->getRateName();
    
    // mixture model without site rate heterogeneity
    if (rate_name.empty())
    {
        // initialize all site's rate equally at 1
        for (int i = 0; i < sequence_length; i++)
        {
            site_specific_rates[i] = 1;
            site_specific_rate_index[i] = RATE_ONE_INDEX;
        }
    }
    // otherwise, it's the case with site rate heterogeneity
    else
    {
        // initalize rates based on continuous gamma distribution
        if ((rate_name.find("+G") != std::string::npos) && tree->getModelFactory()->is_continuous_gamma)
        {
            getSiteSpecificRatesContinuousGamma(site_specific_rates, sequence_length);
        }
        // initalize rates based on discrete distribution (gamma/freerate)
        else
        {
            getSiteSpecificRatesDiscrete(site_specific_rates, sequence_length);
        }
    }
}

/**
*  simulate sequences for all nodes in the tree
*/
void AliSimulatorHeterogeneity::simulateSeqsForTree(string output_filepath){
    // get variables
    int sequence_length = expected_num_sites;
    ModelSubst *model = tree->getModel();
    int max_num_states = tree->aln->getMaxNumStates();
    ostream *out;
    vector<string> state_mapping;
    
    // initialize site specific model index based on its weights (in the mixture model)
    intializeSiteSpecificModelIndex();
    
    // initialize site-specific rates
    double *site_specific_rates = new double[sequence_length];
    getSiteSpecificRates(site_specific_rates, sequence_length);
    
    // initialize trans_matrix
    double *trans_matrix = new double[max_num_states*max_num_states];
    
    // write output to file (if output_filepath is specified)
    if (output_filepath.length() > 0)
    {
        try {
            // add ".phy" or ".fa" to the output_filepath
            if (params->aln_output_format != IN_FASTA)
                output_filepath = output_filepath + ".phy";
            else
                output_filepath = output_filepath + ".fa";
            if (params->do_compression)
                out = new ogzstream(output_filepath.c_str());
            else
                out = new ofstream(output_filepath.c_str());
            out->exceptions(ios::failbit | ios::badbit);

            // write the first line <#taxa> <length_of_sequence> (for PHYLIP output format)
            if (params->aln_output_format != IN_FASTA)
            {
                int num_leaves = tree->leafNum - ((tree->root->isLeaf() && tree->root->name == ROOT_NAME)?1:0);
                *out <<num_leaves<<" "<< round(expected_num_sites/length_ratio)*num_sites_per_state<< endl;
            }

            // initialize state_mapping (mapping from state to characters)
            initializeStateMapping(tree->aln, state_mapping);
        } catch (ios::failure) {
            outError(ERR_WRITE_OUTPUT, output_filepath);
        }
    }
    
    // simulate sequences
    simulateSeqs(sequence_length, site_specific_rates, model, trans_matrix, max_num_states, tree->MTree::root, tree->MTree::root, *out, state_mapping);
    
    // close the file if neccessary
    if (output_filepath.length() > 0)
    {
        if (params->do_compression)
            ((ogzstream*)out)->close();
        else
            ((ofstream*)out)->close();
        delete out;
        
        // show the output file name
        cout << "An alignment has just been exported to "<<output_filepath<<endl;
    }

    // delete trans_matrix array
    delete[] trans_matrix;
    
    // delete the site-specific rates
    delete[] site_specific_rates;
    
    // removing constant states if it's necessary
    if (length_ratio > 1)
        removeConstantSites();
}
