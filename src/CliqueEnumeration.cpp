
#include <cstdlib>
#include <CliqueEnumeration.hpp>
#include <BitStructures.hpp>

namespace kn
{
    // Uncomment to enable pretty printing
    //#define ENABLE_PRETTY_PRINT

    /// The common context for many clique enumerators in the BronKerbosch family.

    class Context
    {
    private:
        IntegerSet* pool;
        IntegerSet* next;

    public:
        std::size_t numVertices;
        std::vector<IntegerSet> N;
        std::vector<IntegerSet> K;

        const Graph* graph;
        CliqueReceiver* receiver;

        Context(const Graph* graph, CliqueReceiver* receiver)
        {
            this->graph = graph;
            this->receiver = receiver;

            numVertices = graph->countVertices();
            for (std::size_t ui = 0; ui < numVertices; ui++)
            {
                IntegerSet neighbours(numVertices);
                Graph::Vertex u;
                Graph::Edge e;
                graph->getVertexByIndex(ui, u);
                for (auto it = graph->exitingEdgeIterator(u.id); it.next(e); )
                {
                    std::size_t vi = graph->getVertexIndex(e.v);
                    if (ui != vi) neighbours.add(vi);
                }

                IntegerSet conflicts(neighbours);
                conflicts.invert();

                N.push_back(neighbours);
                K.push_back(conflicts);
            }

            this->pool = new IntegerSet[4 * (1+numVertices) + 3];
            for (std::size_t i = 0; i < 4 * (1+numVertices) + 3; i++)
            {
                this->pool[i].setMaxCardinality(numVertices);
            }
            this->next = &this->pool[0];
        }

        ~Context()
        {
            delete[] this->pool;
        }

        IntegerSet* reserveSet()
        {
            return next++;
        }

        void releaseSet()
        {
            next--;
        }

        IntegerSet* intersect(IntegerSet* a, IntegerSet* b)
        {
            IntegerSet* r = reserveSet();
            r->intersection(*a, *b);
            return r;
        }

        IntegerSet* insert(IntegerSet* a, std::size_t value)
        {
            IntegerSet* r = reserveSet();
            r->copy(*a);
            r->add(value);
            return r;
        }
    };


    /// The base class for a BronKerbosch clique enumerator.
    class BKSearch : public Context
    {
    public:
        BKSearch(const Graph* graph, CliqueReceiver* receiver) :
            Context(graph, receiver) {}

        void enumerateCliques(bool ordered = false)
        {
            if (!ordered)
            {
                IntegerSet* S = reserveSet();
                IntegerSet* P = reserveSet();
                IntegerSet* X = reserveSet();

                S->clear();
                P->fill();
                X->clear();

                receiver->reset();
                receiver->onClear();
                apply(S, P, X);
                receiver->onComplete();

                // consumed by apply: S, P, X
            }
            else
            {
                receiver->reset();
                receiver->onClear();
                std::size_t n = graph->countVertices();

#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                bool grouped = (n > 1);
                if (grouped) receiver->onOpenGroup();
                bool first = true;
                Graph::Vertex vertex;
#endif

                for (std::size_t k = 0; k < n; k++)
                {
                    IntegerSet* S = reserveSet();
                    IntegerSet* P = reserveSet();
                    IntegerSet* X = reserveSet();

                    S->clear();
                    P->clear();
                    X->clear();

                    S->add(k);
                    P->fillBefore(k);
                    X->fillAfter(k);
                    P->intersectWith(N[k]);
                    X->intersectWith(N[k]);

#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                    if (!first) receiver->onPartition();
                    first = false;
                    graph->getVertexByIndex(k, vertex);
                    receiver->onVertex(k, vertex.attrID);
#endif

                    apply(S, P, X);
                    // consumed by apply: S, P, X
                }

#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                if (first) receiver->onCutOff();
                if (grouped) receiver->onCloseGroup();
#endif
                receiver->onComplete();
            }
        }
            
        void apply(IntegerSet* S, IntegerSet* P, IntegerSet* X)
        {
            receiver->recursionCounter++;
            IntegerSet* Q = pivotConflict(S, P, X);
            if (Q)
            {
#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                bool grouped = (Q->countLimit(2) > 1);
                if (grouped) receiver->onOpenGroup();
                bool first = true;
                Graph::Vertex vertex;
#endif

                auto it = Q->iterator();
                while (it.hasNext())
                {
                    std::size_t v = it.next();
                    P->remove(v);
#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                    if (!first) receiver->onPartition();
                    first = false;
                    graph->getVertexByIndex(v, vertex);
                    receiver->onVertex(v, vertex.attrID);
#endif

                    IntegerSet* s2 = this->insert(S, v);
                    IntegerSet* p2 = this->intersect(P, &N[v]);
                    IntegerSet* x2 = this->intersect(X, &N[v]);

                    apply(s2, p2, x2);

                    X->add(v);
                }

#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                if (first) receiver->onCutOff();
                if (grouped) receiver->onCloseGroup();
#endif
                this->releaseSet(); // Release Q
            }
            else
            if (X->isEmpty())
            {
                /// maximal clique found
                receiver->cliqueCounter++;
                receiver->onClique(*graph, *S);
#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                receiver->onOk();
#endif
            }
            else
            {
                /// cut-off: sub-maximal clique
                receiver->cutOffCounter++;
#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                receiver->onCutOff();
#endif
            }

            this->releaseSet(); // Release X
            this->releaseSet(); // Release P
            this->releaseSet(); // Release S
        }

        virtual IntegerSet* pivotConflict(IntegerSet* S, IntegerSet* P, IntegerSet* X) = 0;
    };


    class BKSearch_Tomita : public BKSearch
    {
    public:
        BKSearch_Tomita(const Graph* graph, CliqueReceiver* receiver) :
            BKSearch(graph, receiver) {}

        virtual IntegerSet* pivotConflict(IntegerSet* S, IntegerSet* P, IntegerSet* X)
        {
            if (!P->isEmpty())
            {
                std::size_t most = 0;
                std::size_t q = 0;

                auto it = X->iterator();
                while (it.hasNext())
                {
                    std::size_t v = it.next();
                    std::size_t count = P->countCommon(N[v]) + 1;
                    if (count > most)
                    {
                        most = count;
                        q = v;
                    }
                }

                auto it2 = P->iterator();
                while (it2.hasNext())
                {
                    std::size_t v = it2.next();
                    std::size_t count = P->countCommon(N[v]) + 1;
                    if (count > most)
                    {
                        most = count;
                        q = v;
                    }
                }

                IntegerSet* Q = this->intersect(P, &K[q]);

                return Q;
            }
            else
            {
                return nullptr;
            }
        }
    };

    class BKSearch_Naude : public BKSearch
    {
    public:
        BKSearch_Naude(const Graph* graph, CliqueReceiver* receiver) :
            BKSearch(graph, receiver) {}

        virtual IntegerSet* pivotConflict(IntegerSet* S, IntegerSet* P, IntegerSet* X)
        {
        search:
            std::size_t q = numVertices; // an initial value which is not a valid vertex
            std::size_t least = numVertices + 1; // not infinity, but large enough
#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
            Graph::Vertex vertex;
#endif

            if (!X->isEmpty())
            {
                auto it = X->iterator();
                while (it.hasNext())
                {
                    std::size_t v = it.next();
                    std::size_t w = 0;
                    std::size_t count = P->countCommonLimit(K[v], least, w);
                    if (count < least)
                    {
                        if (count <= 2)
                        {
                            if (count != 1) // count in { 0, 2 }
                            {
                                q = v;
                                goto conclude;
                            }
                            else
                            {
                                /// Process w in place
                                S->add(w);
                                P->intersectWith(N[w]);
                                X->intersectWith(N[w]);

#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                                graph->getVertexByIndex(w, vertex);
                                receiver->onVertex(w, vertex.attrID);
#endif

                                /// Very important!
                                /// We are iterating through X, and we have potentially just modified X.
                                /// It is essential that the iterator be written to allow concurrent updates.
                                /// If your iterator does not allow concurrent updates, then just "goto search".

                                if (K[w].contains(q)) goto search;
                            }
                        }
                        else
                        {
                            q = v;
                            least = count;
                        }
                    }
                }
            }

            if (!P->isEmpty())
            {
                auto it = P->iterator();
                while (it.hasNext())
                {
                    std::size_t v = it.next();
                    std::size_t w = 0;
                    std::size_t count = P->countCommonLimit(K[v], least, w);
                    if (count < least)
                    {
                        if (count <= 2)
                        {
                            if (count != 1) // count in { 0, 2 }
                            {
                                q = v;
                                goto conclude;
                            }
                            else
                            {
                                /// Process v in place
                                S->add(v);
                                P->intersectWith(N[v]);
                                X->intersectWith(N[v]);

#if !defined(NDEBUG) && defined(ENABLE_PRETTY_PRINT)
                                graph->getVertexByIndex(v, vertex);
                                receiver->onVertex(v, vertex.attrID);
#endif

                                /// Very important!
                                /// We are iterating through P, and we have potentially just modified P.
                                /// It is essential that the iterator be written to allow concurrent updates.
                                /// If your iterator does not allow concurrent updates, then just "goto search".

                                if (K[v].contains(q)) goto search;
                            }
                        }
                        else
                        {
                            q = v;
                            least = count;
                        }
                    }
                }
            }

        conclude:
            if (q < numVertices)
            {
                IntegerSet* Q = this->intersect(P, &K[q]);
                return Q;
            }
            else
                return nullptr;
        }
    };


    void AllCliques_Tomita(const Graph* graph, CliqueReceiver* receiver)
    {
        BKSearch_Tomita alg(graph, receiver);

        alg.enumerateCliques();
    }

    void AllCliques_Naude(const Graph* graph, CliqueReceiver* receiver)
    {
        BKSearch_Naude alg(graph, receiver);

        alg.enumerateCliques();
    }

}
