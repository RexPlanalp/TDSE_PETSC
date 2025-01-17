#pragma once
#include <vector>
#include <unordered_map>

namespace gauss {
    struct Rule {
        std::vector<double> roots;
        std::vector<double> weights;
    };

    // Directly accessible map of quadrature rules
    std::unordered_map<int, Rule> rules = {
        {2, {{-0.57735027, 0.57735027}, {1, 1}}},
        {3, {{-0.77459667, 0.0, 0.77459667}, {0.55555556, 0.88888889, 0.55555556}}},
        {4, {{-0.86113631, -0.33998104, 0.33998104, 0.86113631}, {0.34785485, 0.65214515, 0.65214515, 0.34785485}}},
        {5, {{-0.90617985, -0.53846931, 0.0, 0.53846931, 0.90617985}, {0.23692689, 0.47862867, 0.56888889, 0.47862867, 0.23692689}}},
        {6, {{-0.93246951, -0.66120939, -0.23861919, 0.23861919, 0.66120939, 0.93246951}, {0.17132449, 0.36076157, 0.46791393, 0.46791393, 0.36076157, 0.17132449}}},
        {7, {{-0.94910791, -0.74153119, -0.40584515, 0, 0.40584515, 0.74153119, 0.94910791}, {0.12948497, 0.27970539, 0.38183005, 0.41795918, 0.38183005, 0.27970539, 0.12948497}}},
        {8, {{-0.96028986, -0.79666648, -0.52553241, -0.18343464, 0.18343464, 0.52553241, 0.79666648, 0.96028986}, {0.10122854, 0.22238103, 0.31370665, 0.36268378, 0.36268378, 0.31370665, 0.22238103, 0.10122854}}}
    };

    // Function to get weights for a given order
    std::vector<double> get_weights(int order) 
    {
        if (rules.find(order) != rules.end()) 
        {
            return rules[order].weights;
        } 
        else 
        {
            return {};  // Return empty vector if not found
        }
    }

    std::vector<double> get_roots(int order) 
    {
        if (rules.find(order) != rules.end()) 
        {
            return rules[order].roots;
        } 
        else 
        {
            return {};  
        }
    }
};
