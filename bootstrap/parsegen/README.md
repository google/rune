The algorithm here is mostly from these two sites:

https://lambda.uta.edu/cse5317/notes/node20.html
http://web.cs.dal.ca/~sjackson/lalr1.html

The lambda.uta.edu article mentioned simply building the LR(0) set, and then
adding the "lookahead" sets as a post-process, which is done here.  More details
of how to make this all work is found on the other site.
