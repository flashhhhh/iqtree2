#include "yamlmodelwrapper.h"
#include "model/modelinfo.h"

YAMLModelBinary::YAMLModelBinary(ModelInfoFromYAMLFile& info,
                                 bool make_copy, const char *model_name, 
                                 const std::string& model_params, StateFreqType freq, 
                                 const std::string& freq_params, PhyloTree *tree, 
                                 PhyloTree* report_to_tree)
        : super(info, make_copy, tree, report_to_tree) {
    init(model_name, model_params, freq, freq_params, report_to_tree);
    setNumberOfVariableRates(model_info->getNumberOfVariableRates());
    setRateMatrixFromModel();
}

YAMLModelCodon::YAMLModelCodon(ModelInfoFromYAMLFile& info,
                               bool        make_copy,    const char*   model_name, 
                               const std::string& model_params, StateFreqType freq, 
                               const std::string& freq_params,  PhyloTree*    tree, 
                               PhyloTree*  report_to_tree)
        : super(info, make_copy, tree, report_to_tree) {
    setReversible(info.isReversible());
    init(model_name, model_params, freq, freq_params, report_to_tree);
    setNumberOfVariableRates(model_info->getNumberOfVariableRates());
    setRateMatrixFromModel();
}

YAMLModelDNA::YAMLModelDNA(ModelInfoFromYAMLFile& info,
                           bool               make_copy,    const char*   model_name, 
                           const std::string& model_params, StateFreqType freq, 
                           const std::string& freq_params,  PhyloTree*    tree, 
                           PhyloTree*  report_to_tree)
        : super(info, make_copy, tree, report_to_tree) {
    init(model_name, model_params, freq,
            freq_params, report_to_tree);
    setNumberOfVariableRates(model_info->getNumberOfVariableRates());
    setRateMatrixFromModel();
}

YAMLModelDNAError::YAMLModelDNAError
    (ModelInfoFromYAMLFile& info, bool  make_copy, const char* model_name, 
     const std::string& model_params, StateFreqType freq, 
     const std::string& freq_params,  PhyloTree*    tree, 
     PhyloTree* report_to_tree)                    
        : super(info, make_copy, tree, report_to_tree) {
    init(model_name, model_params, freq,
            freq_params, report_to_tree);
    setNumberOfVariableRates(model_info->getNumberOfRateCategories());
    setRateMatrixFromModel();
}

bool YAMLModelDNAError::getVariables(const double *variables) {
    bool changed = super::getVariables(variables);
    if (changed && !fix_epsilon) {
        epsilon = model_info->getVariableValue("epsilon");
    }
    return changed;
}

YAMLModelMorphology::YAMLModelMorphology(ModelInfoFromYAMLFile& info,
                                         bool        make_copy,    const char* model_name, 
                                         const std::string& model_params, StateFreqType freq, 
                                         const std::string& freq_params,  PhyloTree*    tree, 
                                         PhyloTree*  report_to_tree)
        : super(info, make_copy, tree, report_to_tree) {
    init(model_name, model_params, freq, freq_params, report_to_tree);
    setNumberOfVariableRates(model_info->getNumberOfVariableRates());
    setRateMatrixFromModel();
}

YAMLModelProtein::YAMLModelProtein(ModelInfoFromYAMLFile& info, 
                                   bool               make_copy,    const char*   model_name, 
                                   const std::string& model_params, StateFreqType freq, 
                                   const std::string& freq_params,  ModelsBlock*  block,
                                   PhyloTree*         tree,         PhyloTree*    report_to_tree)
        : super(info, make_copy, tree, report_to_tree) {
    setModelsBlock(block);
    setNumberOfStates(20);
    setReversible(info.isReversible());
    init(model_name, model_params, freq,
            freq_params, report_to_tree);
    setNumberOfStates(20); //Why again?
    setNumberOfVariableRates(model_info->getNumberOfVariableRates());
    setRateMatrixFromModel();
}

YAMLModelMixture::YAMLModelMixture(ModelInfoFromYAMLFile& info,
                                   bool          make_copy, const char*  model_name, 
                                   StateFreqType freq,      ModelsBlock* models_block,
                                   PhyloTree*    tree,      PhyloTree*   report_to_tree)
    : super(info, make_copy, tree, report_to_tree) {
    //as per init() and initMixture()
    ASSERT(info.isMixtureModel());

    StateFreqType old_freq = info.getFrequencyType();
    StateFreqType new_freq = old_freq;
    if (new_freq == StateFreqType::FREQ_UNKNOWN) {
        new_freq = freq;
    }
    if (new_freq == StateFreqType::FREQ_UNKNOWN) {
        new_freq = StateFreqType::FREQ_USER_DEFINED;
    }
    info.setFrequencyType(new_freq);

    bool optimize_weights = false;

    const std::string no_parameters; //parameters are passed to the mixture.
    full_name = "MIX";
    full_name += OPEN_BRACKET;
    const char* separator = "";
    DoubleVector weights;
    for (auto child: model_info->getMixedModels()) {
        auto model = ModelListFromYAMLFile::getModelByReference
                    (*child, tree, info.getFrequencyType(),
                    models_block, no_parameters, 
                    report_to_tree);
        models.push_back(model);
        weights.push_back(child->getModelWeight());
        if (!optimize_weights) {
            optimize_weights = !child->isModelWeightFixed();
        }
        //what about weights?!
        full_name += separator;
        full_name += child->getName();
        separator = ",";
    }
    full_name += CLOSE_BRACKET;

    TREE_LOG_LINE(*report_to_tree, YAMLVariableVerbosity, 
                  "optimize_weights=" << optimize_weights);

    checkProportionsAndWeights(weights);
    setOptimizationSteps(optimize_weights);
    checkModelReversibility();
    decomposeRateMatrix();

    phylo_tree = tree;

    setNumberOfVariableRates(model_info->getNumberOfVariableRates());
    setRateMatrixFromModel();
}

bool YAMLModelMixture::isMixtureModel() const {
    return true;
}

void YAMLModelMixture::setRateMatrixFromModel() {
    //
    //Called when variables have been changed, for at least some of
    //the child models associated with this mixture, during initialization.
    //Each child model has its own copy of the ModelInfoFromYAMLFile
    //(variables and all), which might be out of date; the variables
    //updated in the mixture models in model_info.getMixedModels()
    //need to be copied across to the copies in the child models
    //(and the rate matrices of the child models need to be recalculated).
    //
    //See also afterVariablesChanged() which copies in the other direction
    //during optimization.
    //
    for (ModelMarkov* model : models) {
        model->setRateMatrixFromModel();
    }
}

void YAMLModelMixture::afterVariablesChanged() {
    //
    //Called when variables have been changed, for at least some of
    //the child models associated with this mixture, during optimization.
    //
}

void YAMLModelMixture::afterWeightsChanged() {
    int nmix = getNMixtures();
    if (1<nmix) {
        int i = 0;
        model_info->updateModelVariablesByType(prop, getNMixtures(), true,
                                               ModelParameterType::WEIGHT, i,
                                               phylo_tree);
        return;
    }
}

YAMLRateFree::YAMLRateFree(PhyloTree* tree, PhyloTree* report_to_tree,
                           ModelInfoFromYAMLFile& info)
        : super(info, report_to_tree) {
    setNCategory(info.getNumberOfRateCategories());
    std::string algorithm = info.getOptimizationAlgorithm();
    optimize_alg = algorithm.empty() ? optimize_alg : algorithm;
    gamma_shape  = 1.0;

    //num_rate_cats, gamma_shape,
    //freerate_params, !fused_mix_rate,
    //params.optimize_alg_freerate, tree

    setProportionToleranceFromModel();
    setRateToleranceFromModel();
}

void YAMLRateFree::updateRateClassFromModelVariables() {
    int rate_count = model_info.getNumberOfRateCategories();
    int prop_count = model_info.getNumberOfProportions();
    int rate_ix    = 1;
    int prop_ix    = 1;
    model_info.readModelVariablesByType(rates, rate_count, true,
                                        ModelParameterType::RATE, rate_ix,
                                        phylo_tree);
    model_info.readModelVariablesByType(prop,  prop_count, true,
                                        ModelParameterType::PROPORTION, prop_ix,
                                        phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set rates and props from model variables");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }
}

void YAMLRateFree::sortUpdatedRates() {
    super::sortUpdatedRates();
    int rate_count = model_info.getNumberOfRateCategories();
    int prop_count = model_info.getNumberOfProportions();
    int rate_ix    = 1;
    int prop_ix    = 1;
    model_info.updateModelVariablesByType(rates, rate_count, true,
                                          ModelParameterType::RATE, rate_ix,
                                          phylo_tree);
    model_info.updateModelVariablesByType(prop,  prop_count, true,
                                          ModelParameterType::PROPORTION, prop_ix,
                                          phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set model variables during rate optimization");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }                               
}

YAMLRateInvar::YAMLRateInvar(PhyloTree* tree, PhyloTree* report_to_tree,
                            ModelInfoFromYAMLFile& info)
        : super(info, report_to_tree) {
    std::string          algorithm = info.getOptimizationAlgorithm();
    const ModelVariable* pvar      = info.getInvariantProportionVariable();
    ASSERT(pvar!=nullptr);
    auto                 range     = pvar->getRange();

    setMinimumProportion(range.first);
    setMaximumProportion(range.second);

    YAMLFileParameter&   param     = info.getInvariantProportionParameter();
    if (!param.tolerance_expression.empty()) {
        param.tolerance = info.evaluateExpression(param.tolerance_expression, 
                                                  "invariant proportion");
        setProportionTolerance(param.tolerance);
    }

    defaultInvariantProportion(pvar->getValue());

    fix_p_invar = pvar->isFixed(); 
}

void YAMLRateInvar::updateRateClassFromModelVariables() {
    double dummy_doubles[2];
    int    prop_index = 1;
    model_info.readModelVariablesByType(&dummy_doubles[0],  1, true,
                                        ModelParameterType::INVARIANT_PROPORTION, 
                                        prop_index, phylo_tree);
    p_invar = dummy_doubles[1];
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set invariant propoprtion (" << p_invar 
                      << " from model variables");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }
}

void YAMLRateInvar::sortUpdatedRates() {
    double dummy_doubles[2] = { 0, p_invar };
    int    prop_index = 1;
    super::sortUpdatedRates();

    model_info.updateModelVariablesByType(&dummy_doubles[0],  1, true,
                                          ModelParameterType::INVARIANT_PROPORTION, 
                                          prop_index, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set invariant proportion as part of"
                      " invariant proportion optimization");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }                               
}

YAMLRateFreeInvar::YAMLRateFreeInvar(PhyloTree* tree, PhyloTree* report_to_tree,
                                     ModelInfoFromYAMLFile& info)
        : super(info, report_to_tree) {
    setNCategory(info.getNumberOfRateCategories());
    std::string algorithm = info.getOptimizationAlgorithm();
    if (!algorithm.empty()) {
        setOptimizationAlgorithm(algorithm);
    }
    setGammaShape(1.0);
    setProportionToleranceFromModel();
    setRateToleranceFromModel();

    const ModelVariable* pvar  = info.getInvariantProportionVariable();
    ASSERT(pvar!=nullptr);
    setPInvar(pvar->getValue());
    setFixPInvar(pvar->isFixed());

    auto                 range = pvar->getRange();
    setMinimumProportion(range.first);
    setMaximumProportion(range.second);

    YAMLFileParameter&   param = info.getInvariantProportionParameter();
    if (!param.tolerance_expression.empty()) {
        param.tolerance = info.evaluateExpression(param.tolerance_expression, 
                                                  "invariant proportion");
        invar.setProportionTolerance(param.tolerance);
    }

    //num_rate_cats, gamma_shape,
    //freerate_params, !fused_mix_rate,
    //params.optimize_alg_freerate, tree
}

void YAMLRateFreeInvar::updateRateClassFromModelVariables() {
    int rate_count = model_info.getNumberOfRateCategories();
    int prop_count = model_info.getNumberOfProportions();
    int rate_ix    = 1;
    int prop_ix    = 1;
    TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, "RC=" << rate_count
        << ", PC=" << prop_count);

    model_info.readModelVariablesByType(rates, rate_count, true,
                                        ModelParameterType::RATE, rate_ix,
                                        phylo_tree);
    model_info.readModelVariablesByType(prop,  prop_count, true,
                                        ModelParameterType::PROPORTION, prop_ix,
                                        phylo_tree);
    //Is the last proportion correct?

    prop_ix = prop_count;                                    
    TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, "PI=" << prop_ix);
    model_info.readModelVariablesByType(prop,  prop_count, true,
                                        ModelParameterType::INVARIANT_PROPORTION, 
                                        prop_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set rates and props from model variables");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }
}

void YAMLRateFreeInvar::sortUpdatedRates() {
    super::sortUpdatedRates();
    int rate_count = model_info.getNumberOfRateCategories();
    int prop_count = model_info.getNumberOfProportions();
    int rate_ix    = 1;
    int prop_ix    = 1;
    model_info.updateModelVariablesByType(rates, rate_count, true,
                                          ModelParameterType::RATE, rate_ix,
                                          phylo_tree);
    model_info.updateModelVariablesByType(prop,  prop_count, true,
                                          ModelParameterType::PROPORTION, prop_ix,
                                          phylo_tree);
    model_info.updateModelVariablesByType(prop,  prop_count, true,
                                          ModelParameterType::INVARIANT_PROPORTION, prop_ix,
                                          phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set model variables during rate optimization");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }                               
}

YAMLRateHeterotachy::YAMLRateHeterotachy
    (PhyloTree *tree, PhyloTree* report_to_tree,
     ModelInfoFromYAMLFile& info)
    : super(info, report_to_tree) {
    setNCategory(info.getNumberOfProportions());
    setProportionToleranceFromModel();
}

void YAMLRateHeterotachy::updateRateClassFromModelVariables() {
    int prop_count = model_info.getNumberOfProportions();
    int prop_ix    = 0;
    model_info.readModelVariablesByType(prop,  prop_count, true,
                                        ModelParameterType::PROPORTION, 
                                        prop_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set props from model variables");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }
}

void YAMLRateHeterotachy::sortUpdatedRates() {
    super::sortUpdatedRates();
    int prop_count = model_info.getNumberOfProportions();
    int prop_ix    = 0;
    model_info.updateModelVariablesByType(prop,  prop_count, true,
                                          ModelParameterType::PROPORTION, 
                                          prop_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set model variables during proportion optimization");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }                               
}

YAMLRateHeterotachyInvar::YAMLRateHeterotachyInvar
    (PhyloTree *tree, PhyloTree* report_to_tree,
     ModelInfoFromYAMLFile& info) 
     : super(info, report_to_tree) {
    setNCategory(info.getNumberOfProportions());
    setGammaShape(1.0);
    setProportionToleranceFromModel();

    YAMLFileParameter&   param  = info.getInvariantProportionParameter();
    if (!param.tolerance_expression.empty()) {
        param.tolerance = info.evaluateExpression(param.tolerance_expression, 
                                                  "invariant proportion");
        invar.setProportionTolerance(param.tolerance);
    }

    const ModelVariable* pvar  = info.getInvariantProportionVariable();
    ASSERT(pvar!=nullptr);
    setPInvar(pvar->getValue());
    setFixPInvar(pvar->isFixed());

    auto                 range = pvar->getRange();
    setMinimumProportion(range.first);
    setMaximumProportion(range.second);
}

void YAMLRateHeterotachyInvar::updateRateClassFromModelVariables() {
    int prop_count = model_info.getNumberOfProportions();
    int prop_ix    = 0;
    model_info.readModelVariablesByType(prop,  prop_count, true,
                                        ModelParameterType::PROPORTION, 
                                        prop_ix, phylo_tree);
    model_info.readModelVariablesByType(prop,  prop_count, true,
                                        ModelParameterType::INVARIANT_PROPORTION, 
                                        prop_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set rates and props from model variables");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }
}

void YAMLRateHeterotachyInvar::sortUpdatedRates() {
    super::sortUpdatedRates();
    int prop_count = model_info.getNumberOfProportions();
    int prop_ix    = 0;
    model_info.updateModelVariablesByType(prop,  prop_count, true,
                                          ModelParameterType::PROPORTION, 
                                          prop_ix, phylo_tree);
    model_info.updateModelVariablesByType(prop,  prop_count, true,
                                          ModelParameterType::INVARIANT_PROPORTION, 
                                          prop_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set model variables during proportion optimization");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }                               
}

YAMLRateMeyerDiscrete::YAMLRateMeyerDiscrete(PhyloTree* tree, PhyloTree* report_to_tree,
                           ModelInfoFromYAMLFile& info)
        : super(info, report_to_tree) {
    setNCategory(info.getNumberOfRateCategories());

    //num_rate_cats, gamma_shape,
    //freerate_params, !fused_mix_rate,
    //params.optimize_alg_freerate, tree

    setRateToleranceFromModel();
}

void YAMLRateMeyerDiscrete::updateRateClassFromModelVariables() {
    int rate_count = model_info.getNumberOfRateCategories();
    int rate_ix    = 1;
    model_info.readModelVariablesByType(rates, rate_count, true,
                                        ModelParameterType::RATE, 
                                        rate_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set rates from model variables");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }
}

void YAMLRateMeyerDiscrete::sortUpdatedRates() {
    super::sortUpdatedRates();
    int rate_count = model_info.getNumberOfRateCategories();
    int rate_ix    = 1;
    model_info.updateModelVariablesByType(rates, rate_count, true,
                                          ModelParameterType::RATE, 
                                          rate_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set model variables during rate optimization");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }                               
}

YAMLRateMeyerHaeseler::YAMLRateMeyerHaeseler
    (PhyloTree* tree, PhyloTree* report_to_tree,
     ModelInfoFromYAMLFile& info)
        : super(info, report_to_tree) {

    //num_rate_cats, gamma_shape,
    //freerate_params, !fused_mix_rate,
    //params.optimize_alg_freerate, tree

    setRateToleranceFromModel();
}

void YAMLRateMeyerHaeseler::updateRateClassFromModelVariables() {
}

void YAMLRateMeyerHaeseler::sortUpdatedRates() {
    super::sortUpdatedRates();
}

YAMLRateKategory::YAMLRateKategory(PhyloTree* tree, PhyloTree* report_to_tree,
                           ModelInfoFromYAMLFile& info)
        : super(info, report_to_tree) {
    setNCategory(info.getNumberOfRateCategories());

    //num_rate_cats, gamma_shape,
    //freerate_params, !fused_mix_rate,
    //params.optimize_alg_freerate, tree

    setRateToleranceFromModel();
}

void YAMLRateKategory::updateRateClassFromModelVariables() {
    int rate_count = model_info.getNumberOfRateCategories();
    int rate_ix    = 1;
    model_info.readModelVariablesByType(rates, rate_count, true,
                                        ModelParameterType::RATE, 
                                        rate_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set rates from model variables");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }
}

void YAMLRateKategory::sortUpdatedRates() {
    super::sortUpdatedRates();
    int rate_count = model_info.getNumberOfRateCategories();
    int rate_ix    = 1;
    model_info.updateModelVariablesByType(rates, rate_count, true,
                                          ModelParameterType::RATE, 
                                          rate_ix, phylo_tree);
    if (YAMLRateVerbosity <= verbose_mode) {
        TREE_LOG_LINE(*phylo_tree, YAMLRateVerbosity, 
                      "Set model variables during rate optimization");
        phylo_tree->hideProgress();
        writeInfo(std::cout);
        phylo_tree->showProgress();
    }                               
}
