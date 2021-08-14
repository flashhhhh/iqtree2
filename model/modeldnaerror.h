//
//  modeldnaerror.hpp
//  model
//
//  Created by Minh Bui on 26/6/20.
//

#ifndef modeldnaerror_hpp
#define modeldnaerror_hpp

#include "modeldna.h"

/**
DNA models with sequecing error

    @author BUI Quang Minh <minh.bui@univie.ac.at>
*/
class ModelDNAError : public ModelDNA
{
public:

    /**
        constructor
        @param tree associated tree for the model
    */
    ModelDNAError(PhyloTree *tree, PhyloTree* report_to_tree);

    /**
        constructor
        @param model_name model name, e.g., JC, HKY.
        @param freq state frequency type
        @param tree associated phylogenetic tree
    */
    ModelDNAError(const char *model_name, const string& model_params, 
                  StateFreqType freq, const string& freq_params, 
                  const string& seqerr, PhyloTree *tree, 
                  PhyloTree* report_to_tree);

    /**
        start structure for checkpointing
    */
    virtual void startCheckpoint() override;

    /**
        save object into the checkpoint
    */
    virtual void saveCheckpoint() override;

    /**
        restore object from the checkpoint
    */
    virtual void restoreCheckpoint() override;

    /**
     * @return model name
     */
    virtual string getName() const override;

    /**
     * @return model name with parameters in form of e.g. GTR{a,b,c,d,e,f}
     */
    virtual std::string getNameParams() const override;

    /**
        write information
        @param out output stream
    */
    virtual void writeInfo(ostream &out) override;


    /**
        return the number of dimensions
    */
    virtual int getNDim() const override;

    /**
     * setup the bounds for joint optimization with BFGS
     */
    virtual void setBounds(double *lower_bound, double *upper_bound, 
                           bool *bound_check) override;


    /** compute the tip likelihood vector of a state for Felsenstein's pruning algorithm
     @param state character state
     @param[out] state_lk state likehood vector of size num_states
     */
    virtual void computeTipLikelihood(PML::StateType state, double *state_lk) override;

        //Supporting function
        int getErrorNucleotideState() const;

    /**
         this function is called by ModelListFromYAMLFile::getModelByName
         to configure an instance ofr ModelDNAError.
         @param e
         @param fixed
         @param seqerr
    */
    void setEpsilon(double e, bool fixed, const std::string& seqerr);

protected:

    /**
        this function is served for the multi-dimension optimization. It should pack the model parameters
        into a vector that is index from 1 (NOTE: not from 0)
        @param variables (OUT) vector of variables, indexed from 1
    */
    virtual void setVariables(double *variables) override;

    /**
        this function is served for the multi-dimension optimization. It should assign the model parameters
        from a vector of variables that is index from 1 (NOTE: not from 0)
        @param variables vector of variables, indexed from 1
        @return TRUE if parameters are changed, FALSE otherwise (2015-10-20)
    */
    virtual bool getVariables(const double *variables) override;
    
protected:
    
    /** sequencing error */
    double epsilon;
    
    /** true to fix epsilon */
    bool fix_epsilon;
    
    /** error model name */
    string seqerr_name;
    
};

#endif /* modeldnaerror_hpp */
