/**
 * hdt_document.hpp
 * Author: Thomas MINIER - MIT License 2017-2018
 */

#include "hdt_document.hpp"
#include "triple_iterator.hpp"
#include <HDTEnums.hpp>
#include <HDTManager.hpp>
#include <SingleTriple.hpp>
#include <fstream>
#include <pybind11/stl.h>

using namespace hdt;

/*!
 * Skip `offset` items from an iterator, optimized for HDT iterators.
 * @param it          [description]
 * @param offset      [description]
 * @param cardinality [description]
 */
template <typename T>
inline void applyOffset(T *it, unsigned int offset, unsigned int cardinality) {
  if (offset > 0 && offset >= cardinality) {
    // hdt does not allow to skip past beyond the estimated nb of results,
    // so we may have a few results to skip manually
    unsigned int remainingSteps = offset - cardinality + 1;
    it->skip(cardinality - 1);
    while (it->hasNext() && remainingSteps > 0) {
      it->next();
      remainingSteps--;
    }
  } else if (offset > 0) {
    it->skip(offset);
  }
}

/*!
 * returns true if a file is readable, False otherwise
 * @param  name [description]
 * @return      [description]
 */
inline bool file_exists(const std::string &name) {
  std::ifstream f(name.c_str());
  bool result = f.good();
  f.close();
  return result;
}

/*!
 * Constructor
 * @param file [description]
 */
HDTDocument::HDTDocument(std::string file) {
  hdt_file = file;
  if (!file_exists(file)) {
    throw std::runtime_error("Cannot open HDT file '" + file + "': Not Found!");
  }
  hdt = HDTManager::mapIndexedHDT(file.c_str());
  processor = new QueryProcessor(hdt);
}

/*!
 * Constructor
 * @param file [description]
 */
HDTDocument::HDTDocument(hdt::HDT * newhdt) {
  hdt = newhdt;
}

/*!
 * Destructor
 */
HDTDocument::~HDTDocument() {}

/*!
 * Factory method for generating an in-memory HDT from a turtle file
 * @return [description]
 */
HDTDocument HDTDocument::generate(std::string inputFile, std::string baseUri) {
        try {
            string configFile;
            string options;

            if (!file_exists(inputFile)) {
                throw std::runtime_error("Cannot open input Turtle file '" + inputFile + "': Not Found!");
            }

            RDFNotation notation = TURTLE;

            HDTSpecification spec(configFile);
            spec.setOptions(options);

            HDT *hdt = HDTManager::generateHDT(inputFile.c_str(), baseUri.c_str(), notation, spec, NULL);

            return HDTDocument(hdt);

        } catch (std::exception& e) {
            cerr << "ERROR: " << e.what() << endl;
            return NULL;
        }
}

/*!
 * Save the current in-memory HDT to a persisted HDT.
 * @return [description]
 */
int HDTDocument::saveToHDT(std::string outputFile) {
        try {
            bool generateIndex=false;
            ofstream out;
            hdt_file = outputFile;
            // Save HDT
            out.open(outputFile.c_str(), ios::out | ios::binary | ios::trunc);
            if(!out.good()){
                throw std::runtime_error("Could not open output file.");
            }
            hdt->saveToHDT(out, NULL);
            out.close();

            if(generateIndex) {
                hdt = HDTManager::indexedHDT(hdt, NULL);
            }
            return 0;
        } catch (std::exception& e) {
            cerr << "ERROR: " << e.what() << endl;
            return 1;
        }
}

/*!
 * Get the path to the HDT file currently loaded
 * @return [description]
 */
std::string HDTDocument::getFilePath() { return hdt_file; }

/*!
 * Implementation for Python function "__repr__"
 * @return [description]
 */
std::string HDTDocument::python_repr() {
  return "<HDTDocument " + hdt_file + " (~" + std::to_string(getNbTriples()) +
         " RDF triples)>";
}

/*!
 * Search all matching triples for a triple pattern, whith an optional limit and
 * offset. Returns a tuple<vector<triples>, cardinality>
 * @param subject   [description]
 * @param predicate [description]
 * @param object    [description]
 * @param limit     [description]
 * @param offset    [description]
 */

search_results HDTDocument::search(std::string subject,
                                   std::string predicate,
                                   std::string object,
                                   unsigned int limit,
                                   unsigned int offset) {
  search_results_ids tRes = searchIDs(subject, predicate, object, limit, offset);
  TripleIterator *resultIterator = new TripleIterator(std::get<0>(tRes), hdt->getDictionary());
  return std::make_tuple(resultIterator, std::get<1>(tRes));
}


/*!
 * Same as search, but for an iterator over TripleIDs.
 * Returns a tuple<TripleIDIterator*, cardinality>
 * @param subject   [description]
 * @param predicate [description]
 * @param object    [description]
 * @param limit     [description]
 * @param offset    [description]
 */
search_results_ids HDTDocument::searchIDs(std::string subject,
                                          std::string predicate,
                                          std::string object,
                                          unsigned int limit,
                                          unsigned int offset) {
  
  TripleString ts(subject, predicate, object);
  
  TripleID tid;
  hdt->getDictionary()->tripleStringtoTripleID(ts, tid);

  IteratorTripleID *it;
  size_t cardinality = 0;

  // Make sure that all not-empty resources are in the graph
  if( (tid.getSubject()==0 && !subject.empty()) ||
      (tid.getPredicate()==0 && !predicate.empty()) ||
      (tid.getObject()==0 && !object.empty()) ) {
    it = new IteratorTripleID();
  } else {
    it = hdt->getTriples()->search(tid);
    cardinality = it->estimatedNumResults();
    applyOffset<IteratorTripleID>(it, offset, cardinality);
  }
  // apply offset

  TripleIDIterator *resultIterator =
      new TripleIDIterator(it, subject, predicate, object, limit, offset);
  return std::make_tuple(resultIterator, cardinality);
}

/*!
 * Get the total number of triples in the HDT document
 * @return [description]
 */
unsigned int HDTDocument::getNbTriples() {
  return hdt->getTriples()->getNumberOfElements();
}

/*!
 * Get the number of subjects in the HDT document
 * @return [description]
 */
unsigned int HDTDocument::getNbSubjects() {
  return hdt->getDictionary()->getNsubjects();
}

/*!
 * Get the number of predicates in the HDT document
 * @return [description]
 */
unsigned int HDTDocument::getNbPredicates() {
  return hdt->getDictionary()->getNpredicates();
}

/*!
 * Get the number of objects in the HDT document
 * @return [description]
 */
unsigned int HDTDocument::getNbObjects() {
  return hdt->getDictionary()->getNobjects();
}

/*!
 * Get the number of shared subjects-objects in the HDT document
 * @return [description]
 */
unsigned int HDTDocument::getNbShared() {
  return hdt->getDictionary()->getNshared();
}

/*!
 * Convert a TripleID to a string triple pattern
 * @param  subject   [description]
 * @param  predicate [description]
 * @param  object    [description]
 * @return           [description]
 */
triple HDTDocument::idsToString(unsigned int subject, unsigned int predicate,
                                unsigned int object) {
  return std::make_tuple(
      hdt->getDictionary()->idToString(subject, hdt::SUBJECT),
      hdt->getDictionary()->idToString(predicate, hdt::PREDICATE),
      hdt->getDictionary()->idToString(object, hdt::OBJECT));
}

JoinIterator * HDTDocument::searchJoin(std::vector<triple> patterns) {
  set<string> vars {};
  vector<TripleString> joinPatterns {};
  std::string subj, pred, obj;

  for (auto it = patterns.begin(); it != patterns.end(); it++) {
    // unpack pattern
    std::tie(subj, pred, obj) = *it;
    // add variables
    if (subj.at(0) == '?') {
      vars.insert(subj);
    }
    if (pred.at(0) == '?') {
      vars.insert(pred);
    }
    if (obj.at(0) == '?') {
      vars.insert(obj);
    }
    // build join pattern
    TripleString pattern(subj, pred, obj);
    joinPatterns.push_back(pattern);
  }

  VarBindingString *iterator = processor->searchJoin(joinPatterns, vars);
  return new JoinIterator(iterator);
}
