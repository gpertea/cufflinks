/*
 *  graph_optimize.cpp
 *  cufflinks
 *
 *  Created by Cole Trapnell on 6/1/10.
 *  Copyright 2010 Cole Trapnell. All rights reserved.
 *
 */

#include <vector>

#include "graph_optimize.h"
// for graph optimization only

#include <lemon/topology.h>
#include <lemon/smart_graph.h>
#include <lemon/bipartite_matching.h>

#include "scaffold_graph.h"
#include "scaffolds.h"
#include "filters.h"
#include "matching_merge.h"

using namespace std;
using namespace boost;

namespace ublas = boost::numeric::ublas;

void fill_gaps(vector<Scaffold>& scaffolds, int fill_size)
{
	for (size_t i = 0; i < scaffolds.size(); ++i)
		scaffolds[i].fill_gaps(fill_size);
}

enum ConflictState { UNKNOWN_CONFLICTS = 0, SAME_CONFLICTS, DIFF_CONFLICTS };

bool scaff_lt_rt(const Scaffold& lhs, const Scaffold& rhs)
{
    if (lhs.left() != rhs.left())
        return lhs.left() < rhs.left();
    return lhs.right() < rhs.right();
}

// WARNING: scaffolds MUST be sorted by scaff_lt_rt() in order for this routine
// to work correctly.
void add_non_constitutive_to_scaffold_mask(const vector<Scaffold>& scaffolds,
										   vector<bool>& scaffold_mask)
{	
	//scaffold_mask = vector<bool>(scaffolds.size(), 0);
	for (size_t i = 0; i < scaffolds.size(); ++i)
	{
        //if (!scaffold_mask[i])
        {
            for (size_t j = i+1; j < scaffolds.size(); ++j)
            {
                if (Scaffold::overlap_in_genome(scaffolds[i], scaffolds[j], 0))
                {
                    if (!Scaffold::compatible(scaffolds[i], scaffolds[j]))
                    {
                        scaffold_mask[i] = true;
                        scaffold_mask[j] = true;
                        //break;
                    }
                }
                else
                {
                    break;
                }
            }
        }
	}
}





bool collapse_contained_transfrags(vector<Scaffold>& scaffolds, 
                                   uint32_t max_rounds)
{
	// The containment graph is a bipartite graph with an edge (u,v) when
	// u is (not necessarily properly) contained in v and is the two are
	// compatible.
	typedef lemon::SmartBpUGraph ContainmentGraph;
	normal norm(0, 0.1);
	bool performed_collapse = false;
    
	while (max_rounds--)
	{
		
#if ASM_VERBOSE
		fprintf(stderr, "%s\tStarting new collapse round\n", bundle_label->c_str());
#endif
        
		ContainmentGraph containment;
		
		
		typedef pair<ContainmentGraph::ANode, ContainmentGraph::BNode> NodePair;
		vector<NodePair> node_ids;
		vector<size_t> A_to_scaff(scaffolds.size());
		vector<size_t> B_to_scaff(scaffolds.size());
		
		for (size_t n = 0; n < scaffolds.size(); ++n)
		{
			NodePair p = make_pair(containment.addANode(),
								   containment.addBNode());
			node_ids.push_back(p);
			A_to_scaff[containment.aNodeId(p.first)] = n;
			B_to_scaff[containment.bNodeId(p.second)] = n;
		}
		
		bool will_perform_collapse = false;
		for (size_t i = 0; i < scaffolds.size(); ++i)
		{
			for (size_t j = 0; j < scaffolds.size(); ++j)
			{
				if (i == j)
					continue;
                
				if (scaffolds[i].contains(scaffolds[j]) &&
                    Scaffold::compatible(scaffolds[i], scaffolds[j]))
				{
                    // To gaurd against the identity collapse, which won't 
					// necessary reduce the total number of scaffolds.
					if (scaffolds[j].contains(scaffolds[i]) && i < j)
						continue;
					const NodePair& nj = node_ids[j];
					const NodePair& ni = node_ids[i];
					assert (nj.first != ni.second);
                    
					will_perform_collapse = true;
					ContainmentGraph::UEdge e = containment.addEdge(nj.first,
																	ni.second);						
                    
                }
			}
		}
		
		if (will_perform_collapse == false)
			return performed_collapse;
		
		lemon::MaxBipartiteMatching<ContainmentGraph>  matcher(containment);
        
#if ASM_VERBOSE
		fprintf(stderr, "%s\tContainment graph has %d nodes, %d edges\n", bundle_label->c_str(), containment.aNodeNum(), containment.uEdgeNum());
		fprintf(stderr, "%s\tFinding a maximum matching to collapse scaffolds\n", bundle_label->c_str());
#endif
        
		matcher.run();
        
#if ASM_VERBOSE
		fprintf(stderr, "%s\tWill collapse %d scaffolds\n", bundle_label->c_str(), matcher.matchingSize());
#endif
		
		ContainmentGraph::UEdgeMap<bool> matched_edges(containment);
		
		matcher.matching(matched_edges);
		
        merge_from_matching(containment, matcher, scaffolds);
        
		performed_collapse = true;
	}
	return performed_collapse;
}

bool collapse_equivalent_transfrags(vector<Scaffold>& scaffolds, 
                                    uint32_t max_rounds)
{
	// The containment graph is a bipartite graph with an edge (u,v) when
	// u is (not necessarily properly) contained in v and is the two are
	// compatible.
	typedef lemon::SmartBpUGraph ContainmentGraph;
	normal norm(0, 0.1);
	bool performed_collapse = false;
    
	while (max_rounds--)
	{
		
#if ASM_VERBOSE
		fprintf(stderr, "%s\tStarting new collapse round\n", bundle_label->c_str());
#endif
#if ASM_VERBOSE
        fprintf(stderr, "%s\tFinding fragment-level conflicts\n", bundle_label->c_str());
#endif
        bool will_perform_collapse = false;
        vector<vector<size_t> > conflicts(scaffolds.size());
        
        for (size_t i = 0; i < scaffolds.size(); ++i)
        {
            for (size_t j = i; j < scaffolds.size(); ++j)
            {
                if (i == j)
                    continue;
                if (Scaffold::overlap_in_genome(scaffolds[i], scaffolds[j], 0))
                {
                    if (!Scaffold::compatible(scaffolds[i], scaffolds[j]))
                    {
                        conflicts[i].push_back(j);
                        conflicts[j].push_back(i);
                    }
                }
                else
                {
                    break;
                }
            }
            
        }
        
        for (size_t i = 0; i < scaffolds.size(); ++i)
        {   
            sort(conflicts[i].begin(), conflicts[i].end());
        }
        
#if ASM_VERBOSE
        fprintf(stderr, "%s\tAssessing overlaps between %lu fragments for identical conflict sets\n", 
                bundle_label->c_str(), 
                scaffolds.size());
#endif
        
        vector<size_t> replacements;
        for (size_t i = 0; i < scaffolds.size(); ++i)
        {
            replacements.push_back(i);
        }
        
        for (size_t i = 0; i < scaffolds.size(); ++i)
        {
            size_t lhs = replacements[i];
            
            if (lhs == i)
            {
#if ASM_VERBOSE
                //fprintf (stderr, "Processing %lu (via %lu)\n", i, lhs);
#endif
            
                for (size_t j = i+1; j < scaffolds.size(); ++j)
                {
                    
                    if (Scaffold::overlap_in_genome(scaffolds[lhs], scaffolds[j], 0) &&
                        scaffolds[lhs].contains(scaffolds[j]))
                    {
                        // conflicts needs to be invariant over this whole loop
                        if (conflicts[lhs] == conflicts[j])
                        {
                            
                            assert (Scaffold::compatible(scaffolds[lhs], scaffolds[j]));
                            
                            vector<Scaffold> s;
                            s.push_back(scaffolds[lhs]);
                            s.push_back(scaffolds[j]);
                            scaffolds[lhs] = Scaffold(s);
                            replacements[j] = lhs;
                            will_perform_collapse = true;
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
        }
		
		if (will_perform_collapse == false)
			return performed_collapse;
		

        vector<Scaffold> replaced;
        for (size_t i = 0; i < scaffolds.size(); ++i)
        {
            if (replacements[i] == i)
            {
                replaced.push_back(scaffolds[i]);
            }
        }
        
        scaffolds = replaced;
        sort(scaffolds.begin(), scaffolds.end(), scaff_lt_rt);
		performed_collapse = true;
	}
	return performed_collapse;
}

void compress_consitutive(vector<Scaffold>& hits)
{
    vector<bool> scaffold_mask;
    
#if ASM_VERBOSE
    fprintf(stderr, "%s\tBuilding constitutivity mask\n", bundle_label->c_str()); 
#endif
    
    scaffold_mask = vector<bool>(hits.size(), false);
    add_non_constitutive_to_scaffold_mask(hits, scaffold_mask);
    
    vector<Scaffold> constitutive;
    vector<Scaffold> non_constitutive;
    
    for (size_t i = 0; i < scaffold_mask.size(); ++i)
    {
        if (!scaffold_mask[i])
            constitutive.push_back(hits[i]);
        else
            non_constitutive.push_back(hits[i]);
    }
    
#if ASM_VERBOSE
    size_t pre_compress = hits.size();
#endif
    hits.clear();
    if (!constitutive.empty())
    {
        Scaffold compressed = Scaffold(constitutive); 
        vector<Scaffold> completes; 
        compressed.get_complete_subscaffolds(completes);
        hits.insert(hits.end(), completes.begin(), completes.end()); 
    }
    
    hits.insert(hits.end(), non_constitutive.begin(), non_constitutive.end());
    sort(hits.begin(), hits.end(), scaff_lt);
#if ASM_VERBOSE
    size_t post_compress = hits.size();
    size_t delta = pre_compress - post_compress;
    double collapse_ratio = delta / (double) pre_compress; 
    fprintf(stderr, 
            "%s\tCompressed %lu of %lu constitutive fragments (%lf percent)\n", 
            bundle_label->c_str(),
            delta, 
            pre_compress, 
            collapse_ratio);
#endif
}

void compress_fragments(vector<Scaffold>& fragments)
{
    //compress_consitutive(fragments);
    
#if ASM_VERBOSE
    fprintf(stderr,"%s\tPerforming preliminary containment collapse on %lu fragments\n", bundle_label->c_str(), fragments.size());
    size_t pre_hit_collapse_size = fragments.size();
#endif
    
    sort(fragments.begin(), fragments.end(), scaff_lt_rt);
    
    double last_size = -1;
    long leftmost = 9999999999;
    long rightmost = -1;
    
    for (size_t i = 0; i < fragments.size(); ++i)
    {
        leftmost = std::min((long)fragments[i].left(), leftmost);
        rightmost = std::max((long)fragments[i].right(), rightmost);
    }
    
    long bundle_length = rightmost - leftmost;
    
    while (true)
    {
    
        vector<int>  depth_of_coverage(bundle_length,0);
        map<pair<int,int>, int> intron_depth_of_coverage;
        compute_doc(leftmost,
                    fragments,
                    depth_of_coverage,
                    intron_depth_of_coverage,
                    false,
                    true);
        
        vector<int>::iterator new_end =  remove(depth_of_coverage.begin(), depth_of_coverage.end(), 0);
        depth_of_coverage.erase(new_end, depth_of_coverage.end());
        sort(depth_of_coverage.begin(), depth_of_coverage.end());
        
        size_t median = floor(depth_of_coverage.size() / 2);
        
    //#if ASM_VERBOSE
        fprintf(stderr, "%s\tMedian depth of coverage is %d\n", bundle_label->c_str(), depth_of_coverage[median]);
    //#endif

        if (!depth_of_coverage.empty() && 
            depth_of_coverage[median] > collapse_thresh &&
            (last_size == -1 || 0.9 * last_size > fragments.size()))
        {
            //                    size_t pre_collapse = hits.size();
            //                    strict_containment_collapse(hits);
            //                    size_t post_collapse = hits.size();
            //                    if (pre_collapse == post_collapse)
            //                        break;
            //                    if (!collapse_contained_scaffolds(hits, 1))
            //                        break;
            last_size = fragments.size();
            if (!collapse_equivalent_transfrags(fragments, 1))
            {
                break;
            }
        }
        else
        {
            break;
        }
    }
   
#if ASM_VERBOSE
    vector<int>  depth_of_coverage(bundle_length,0);
    map<pair<int,int>, int> intron_depth_of_coverage;
    compute_doc(leftmost,
                fragments,
                depth_of_coverage,
                intron_depth_of_coverage,
                false,
                true);
    
    vector<int>::iterator new_end =  remove(depth_of_coverage.begin(), depth_of_coverage.end(), 0);
    depth_of_coverage.erase(new_end, depth_of_coverage.end());
    sort(depth_of_coverage.begin(), depth_of_coverage.end());
    
    size_t median = floor(depth_of_coverage.size() / 2);
    
    //#if ASM_VERBOSE
    fprintf(stderr, "%s\tFinal median depth of coverage is %d\n", bundle_label->c_str(), depth_of_coverage[median]);
    
#endif
    compress_consitutive(fragments);
    
#if ASM_VERBOSE
    size_t post_hit_collapse_size = fragments.size();
    fprintf(stderr,"%s\tIgnoring %lu strictly contained fragments\n", bundle_label->c_str(), pre_hit_collapse_size - post_hit_collapse_size);
#endif   
}

void compress_overlap_dag_paths(DAG& bundle_dag,
                                vector<Scaffold>& hits)
{
    HitsForNodeMap hits_for_node = get(vertex_name, bundle_dag);
    
    vector_property_map<size_t> path_for_scaff;
    path_compress_visitor<vector_property_map<DAGNode>,
    vector_property_map<size_t> > vis(path_for_scaff);
    depth_first_search(bundle_dag,
                       visitor(vis));
    
    vector<vector<Scaffold> > compressed_paths(hits.size()+1);

    vector<Scaffold> new_scaffs;
    
    for (size_t i = 0; i < num_vertices(bundle_dag); ++i)
    {
        size_t path_id = path_for_scaff[i];
        assert (path_id < compressed_paths.size());
        const Scaffold* h = hits_for_node[i];
        if (h)
        {
            compressed_paths[path_id].push_back(*h);
        }
    }
    for (size_t i = 0; i < compressed_paths.size(); ++i)
    {
        if (!compressed_paths[i].empty())
        {
            //fprintf(stderr, "Path %d has %d fragments in it\n", i, compressed_paths[i].size());
            new_scaffs.push_back(Scaffold(compressed_paths[i]));
        }
    }
    //hits = new_scaffs;
    fprintf(stderr, "%s\tCompressed overlap graph from %d to %d fragments (%f percent)\n",
            bundle_label->c_str(), 
            hits.size(), 
            new_scaffs.size(), 
            (hits.size() - new_scaffs.size())/(double)hits.size());
    hits = new_scaffs;
    sort(hits.begin(), hits.end(), scaff_lt);
    create_overlap_dag(hits, bundle_dag);
}
