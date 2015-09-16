/*
 * moses/moses/representation/field_set.h
 *
 * Copyright (C) 2002-2008 Novamente LLC
 * All Rights Reserved
 *
 * Written by Moshe Looks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License v3 as
 * published by the Free Software Foundation and including the exceptions
 * at http://opencog.org/wiki/Licenses
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program; if not, write to:
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef _REP_FIELD_SET_H
#define _REP_FIELD_SET_H

#include <map>

#include <boost/operators.hpp>  // for random_access_iterator_helper
#include <boost/variant.hpp>

#include <opencog/util/dorepeat.h>
#include <opencog/util/Logger.h>
#include <opencog/util/mt19937ar.h>
#include <opencog/util/numeric.h>
#include <opencog/util/oc_assert.h>
#include <opencog/util/RandGen.h>
#include <opencog/util/Counter.h>

#include "instance.h"

namespace opencog {
namespace moses {

 /**
  * A field set describes a compact encoding of a set of continuous,
  * discrete and 'term algebra' variables.  The field set does not
  * itself hold the values of these variables; it simply describes
  * how they are packed into a bit-string (the 'instance'). The field
  * set provides a collection of iterators for walking over the fields
  * in an instance; the iterators can also be used to extract values
  * from the instance (or to change them).
  *
  * Some terminology:
  * 'Discrete' variables, or discs, are just variables that range over
  * a set of n discrete values. These take ciel(log_2(n)) bits to store.
  *
  * 'Boolean' or 'bit' variables. This is a special case of the discrete
  * variables, and are handled distinctly in the code and API's below:
  * they have their own iterators, etc. Only the multi-bit discrete
  * variables are called 'disc'.
  *
  * 'Continuous' variables are variables that can range over a continuum.
  * Although one might want to think of these as floats or doubles,
  * they're not.  They're represented very, very differently: as
  * variable-length bit strings which encode intervals on the real-number
  * line. The motivation for this, as opposed to using floats or doubles,
  * is that EDA algorithms are more efficient for such an encoding.
  * Further details, motivation and documentation can be found here:
  * http://code.google.com/p/moses/wiki/ModelingAtomSpaces
  *
  * 'Term algebra' variables, or 'term's, are variables that take values
  * in a term algebra (aka an 'absolutely free algebra').  Terms are
  * best understood as node-labelled trees. The labels occuring as leaves
  * of a tree are commonly called 'constants' or 'zero-ary functions',
  * while the internal nodes of the tree would be called 'n-ary functions'
  * (presuming that they have n children).  The labels on nodes are
  * strings.  Note that atoms (in the general sense, not the opencog
  * sense) are terms with truth values (predicates);  however no atoms or
  * truth values are used here.  See for example, Baader & Nipkow,
  * 'Term Rewriting and All That', for more information. Or wikipedia.
  *
  * Variables are described in terms of 'specs'; there's a disc_spec,
  * a contin_spec, etc.  All variables are stored in the bit string as
  * 'raw' fields.  A single disc_spec corresponds to exactly one
  * 'raw' field; however, a single contin_spec or a single term_spec
  * usually consists of many 'raw' fields.  Thus, raw fields and field
  * specs are NOT in 1-1 correspondance.  Note that all raw fields may
  * be treated as disc fields: the raw field iterator is essentially
  * the same as the disc field iterator, just ranging over a larger set.
  *
  * The raw fields are packed into bit strings, which are chunked
  * as vector arrays of 32 or 64-bit unsigned ints, depending on the
  * C library execution environment. Thus, instead of having a single
  * offset, two are used: a "major offset", pointing to the appropriate
  * 32/64-bit int, and a "minor offset", ranging over 0-31/63. The
  * "width" is the width of the raw field, in bits.
  */
struct field_set
{
    // To avoid the accidental confusion between the multiplicity of
    // values that a variable can take, and the width of a bit-field,
    // (which is ciel(log_2()) of the former), we define two distinct
    // types.  A third type, combo::arity_t is reserved for the arity
    // of a function.  For binary trees, two more types are introduced:
    // depth_t and breadth_t, corresponding to the depth and breadth of
    // a binary tree. These are five distinct concepts, they should
    // not be confused with one-another.
    typedef unsigned multiplicity_t;
    typedef unsigned width_t;
    typedef unsigned breadth_t;
    typedef unsigned depth_t;
    typedef std::size_t size_t;

    struct bit_iterator;
    struct const_bit_iterator;
    struct disc_iterator;
    struct const_disc_iterator;
    struct contin_iterator;
    struct const_contin_iterator;
    struct const_term_iterator;

    /**
     * The field struct provides the information needed to get/set the
     * value of a 'raw' field in an instance (i.e. in the array of ints).
     *
     * The raw fields are packed into bit strings, which are chunked
     * as vector arrays of 32 or 64-bit unsigned ints, depending on the
     * C library execution environment. Thus, instead of having a single
     * bit offset, two are used: the "major_offset", pointing to the
     * appropriate 32/64-bit int, and the "minor_offset", ranging over
     * 0-31/63. The "width" is the width of the raw field, in bits.
     *
     * See also the wiki page
     * http://www.opencog.org/wiki/Field_set_struct_in_MOSES
     */
    struct field
    {
        field() { }
        field(width_t w, size_t ma, size_t mi)
                : width(w), major_offset(ma), minor_offset(mi) { }
        width_t width;
        size_t major_offset, minor_offset;
    };
    typedef std::vector<field>::const_iterator field_iterator;

    /**
     * Specification for a discrete variable.
     * These are in 1-1 correspondence with raw fields.
     */
    struct disc_spec
    {
        disc_spec(multiplicity_t a) : multy(a) { }
        multiplicity_t multy;
        bool operator<(const disc_spec& rhs) const { //sort descending by multy
            return multy > rhs.multy;
        }
        bool operator==(const disc_spec& rhs) const { //don't know why this is needed
            return multy == rhs.multy;
        }
    };

    struct contin_spec
    {
        contin_spec(contin_t start_value, depth_t d)
            : _space(start_value) {
            _tspc = pow2(d);
        }
        mutable contin_t _space, // Current space to search
                         _tspc; // Total space to search
        mutable std::vector<contin_t> _likely;

        bool operator<(const contin_spec& rhs) const { //sort descending by multy
            return _space > rhs._space;
        }

        bool operator==(const contin_spec& rhs) const { //don't know why this is needed
            return _space == rhs._space;
        }

        contin_t get_start() const {
            contin_t start_value = _space;
            _space = _tspc;
            return start_value;
        }

        contin_t get_new(const contin_t& current,
                unsigned dist, opencog::RandGen& rng) const {
            contin_t ret;
            if(_likely.empty()) // || rng.randdouble() < 0.1)
                // XXX Test a chaotic approach 10% of random above
                // XXX Test random restart at dist > 1
                //if(dist > 1)
                //    ret = rand_at_space(_tspc, rng);
                //else
                //    ret = rand_at_space(_space, rng);
                ret = rand_at_space(_space * dist, rng);
            else {
                ret = _likely.back();
                _likely.pop_back();
            }
            return ret;
        }

        static contin_t rand_at_space(const contin_t& space,
                opencog::RandGen& rng = randGen()){
            return rng.randdouble() * rng.randPositiveNegative() * space;
        }

        void compress() const {
            // XXX Test gradient descent
            _likely.clear();
            _space /= 2;
        }

        void set_likely(contin_t value) const {
            // XXX Test order likely by score
             _likely.push_back(value);
        }

    };

    /**
     * Specify a term-algebra-valued variable.
     *
     * Note that a single term_spec requires multiple raw fields
     * to store a value. The number of raw fields needed is given by
     * the 'branching' member (?? err, not sure about that...).
     */
    struct term_spec
    {
        term_spec(const term_tree& t)
                : tr(&t), depth(t.max_depth(t.begin())),
                branching(next_power_of_two(1 + t.max_branching(t.begin()))) { }
        // @todo: could be a source of bug if such order is not total
        // as it's gonna make problems with field_set(from, to)
        bool operator<(const term_spec& rhs) const { //sort descending by size
            return (depth*branching > rhs.depth*rhs.branching);
        }

        const term_tree* tr;
        size_t depth, branching;

        bool operator==(const term_spec& rhs) const { //don't know why this is needed
            return (depth == rhs.depth && branching == rhs.branching && *tr == *(rhs.tr));
        }

        static const disc_t Stop;
        static disc_t to_child_idx(disc_t d) {
            return d -1;
        }
        static disc_t from_child_idx(disc_t d) {
            return d + 1;
        }
    };

    /// A spec, in general, is one of the above three specs.
    typedef boost::variant<term_spec, contin_spec, disc_spec> spec;

    /// Default constructor for an empty field set
    field_set() : _nbool(0)
    {
        compute_starts();
    }

    /// Copy constructor
    field_set(const field_set& x)
        : _fields(x._fields), _term(x._term),
        _disc(x._disc), _nbool(x._nbool), _contin(x._contin)
    {
        compute_starts();
    }

    /// Constructor for a single spec, repeated n times
    field_set(const spec& s, size_t n) : _nbool(0)
    {
        build_spec(s, n);
        compute_starts();
    }

    /// Constructor for a range of specs.
    template<typename It>
    field_set(It from, It to) : _nbool(0)
    {
        // Adding spec's to the map will cause identical specs to be
        // merged (and the count incremented for each).  Non-identical
        // specs are sorted. The sorting seems to follow the order of
        // the spec boost::variant.  Also, since disc_spec::operator<
        // compares by descending multiplicity, it is ensured that the
        // single-bit bit_spec's are at the end.
        Counter<spec, size_t> spec_counts(from, to);

        for (const auto& v : spec_counts) //build them
            build_spec(v.first, v.second);

        compute_starts();                 //compute iterator positions
    }

    // Assignment and equality
    field_set& operator=(const field_set&);
    bool operator==(const field_set&) const;

    size_t packed_width() const {
        return _fields.empty() ? 0 : _fields.back().major_offset + 1;
    }

    bool empty() const {
        return _fields.empty();
    }

    size_t raw_size() const {
        return _fields.size();
    }

    // How many bytes are we burning ip?
    size_t byte_size() const
    {
        size_t sz = sizeof(field_set);
        sz += _fields.size() * sizeof(field);
        sz += _term.size() * sizeof(term_spec);
        sz += _disc.size() * sizeof(disc_spec);
        sz += _contin.size() * sizeof(contin_t);
        return sz;
    }

    // Dimension size, number of actual knobs to consider, as term and
    // contin may take several raw knobs
    size_t dim_size() const {
        return n_bits() + n_disc_fields() + _contin.size() + term().size();
    }

    // Counts the number of nonzero (raw) settings in an instance.
    size_t count(const instance& inst) const
    {
        return raw_size() - std::count(begin_raw(inst), end_raw(inst), 0);
    }

    // Return vector of discrete specs. This vector includes the single
    // bit (boolean) specs, which are at the end of the array, thus the
    // name "disc and bit".
    const std::vector<disc_spec>& disc_and_bit() const
    {
        return _disc;
    }
    const std::vector<term_spec>& term() const
    {
        return _term;
    }
    const std::vector<contin_spec>& contin() const
    {
        return _contin;
    }

    std::vector<contin_spec>& get_contin()
    {
        return _contin;
    }

    // Return the 'idx'th raw field value in the instance.
    disc_t get_raw(const packed_vec& inst, size_t idx) const
    {
        const field& f = _fields[idx];
        return ((inst[f.major_offset] >> f.minor_offset) & ((packed_t(1) << f.width) - 1UL));
    }

    // Return the 'idx'th raw field value in the instance.
    disc_t get_disc_raw(const instance& inst, size_t idx) const
    {
        return get_raw(inst._bit_disc, idx);
    }

    void set_raw(packed_vec& inst, size_t idx, disc_t v) const
    {
        const field& f = _fields[idx];
        inst[f.major_offset] ^= ((inst[f.major_offset] ^
                            (packed_t(v) << f.minor_offset)) &
                            (((packed_t(1) << f.width) - 1UL) << f.minor_offset));
    }

    // returns a reference of the term at idx, idx is relative to
    // term_iterator
    const term_t& get_term(const packed_vec& inst, size_t idx) const;

    // returns the contin at idx, idx is relative to contin_iterator
    contin_t get_contin(const packed_vec& inst, size_t idx) const;
    void set_contin(packed_vec& inst, size_t idx, contin_t v) const;

    // pack the data in [from,from+dof) according to our scheme, copy to out
    template<typename It, typename Out>
    Out pack(It from, Out out) const;

    std::string to_string(const instance&) const;
    std::string to_string_raw(const instance&) const;

    /// Compute the Hamming distance between two instances.
    int hamming_distance(const instance& inst1, const instance& inst2) const
    {
        OC_ASSERT(inst1.size() == inst1.size());
        int d = 0;
        for (const_disc_iterator it1 = begin_raw(inst1), it2 = begin_raw(inst2);
                it1 != end_raw(inst1); ++it1, ++it2)
            d += (*it1 != *it2);
        return d;
    }

    /// Copy fields that differ between base and reference, to the target.
    ///
    /// This routine will iterate over all the fields in the base instance.
    /// If a field value differs between the base and the reference
    /// instance, then the reference value will be copied to the target.
    ///
    /// The intended use of this is to merge two high-scoring instances
    /// into one. Thus, typically, both target and reference will be high
    /// scorers, and base a previous high scorer. Then the difference
    /// (reference minus base) are those bits that made reference into
    /// such a great instance -- so copy those fields into the target.
    /// For many simple hill-climbing, this actually works, because high
    /// scoring knob settings are strongly correlated, even if we don't
    /// really know what these are (i.e. have not used an estimation-of-
    /// distribution/Bayesian-optimization algorithm to figure out the
    /// correlations). That is, we just blindly assume a correlation, and
    /// hope for the best.
    //
    void merge_instance(instance& target,
                        const instance& base,
                        const instance& reference) const
    {
        OC_ASSERT(base.size() == reference.size() and
                  base.size() == target.size());
        disc_iterator tit = begin_raw(target);
        for (const_disc_iterator bit = begin_raw(base),
                                 rit = begin_raw(reference);
             bit != end_raw(base); ++bit, ++rit, ++tit)
        {
            if (*bit != *rit) *tit = *rit;
        }
        target._contin = reference._contin;
    }

    // The fields are organized so that term fields come first,
    // followed by the continuous fields, and then the discrete
    // fields. These are then followed by the 1-bit (boolean)
    // discrete fields, tacked on at the very end. Thus, the
    // start/end values below reflect this structure.
    //
    // Note that the field spec iterates over the 'raw' fields.
    // Multiple raw fields are needed to describe a single contin_spec
    // or term_spec.  By contrast, disc_specs and raw fields are in
    // one-to-one correspondance.
    field_iterator begin_term_fields() const {
        return _fields.begin();
    }
    field_iterator end_term_fields() const {
        return _disc_start;
    }

    field_iterator begin_disc_fields() const {
        return _disc_start;
    }
    field_iterator end_disc_fields() const {
        return _fields.end() - _nbool;
    }

    field_iterator begin_bit_fields() const {
        return _fields.end() - _nbool;
    }
    field_iterator end_bit_fields() const {
        return _fields.end();
    }

    field_iterator begin_fields() const {
        return _fields.begin();
    }
    field_iterator end_fields() const {
        return _fields.end();
    }

    // Same as above, but instead gives the upper and lower bounds
    // for the raw field index.
    size_t begin_term_raw_idx() const {
        return 0;
    }
    size_t end_term_raw_idx() const {
        return _end_term_raw_idx;
    }

    size_t begin_disc_raw_idx() const {
        return _begin_disc_raw_idx;
    }
    size_t end_disc_raw_idx() const {
        return _end_disc_raw_idx;
    }

    size_t begin_bit_raw_idx() const {
        return _begin_bit_raw_idx;
    }
    size_t end_bit_raw_idx() const {
        return _end_bit_raw_idx;
    }

    //* number of discrete fields that are single bits
    //* (i.e. are booleans)
    size_t n_bits() const
    {
        return _nbool;
    }

    //* number of discrete fields, not counting the booleans
    //* i.e. not counting the 1-bit discrete fields.
    size_t n_disc_fields() const
    {
        return _n_disc_fields;
    }

    //* number of raw contin fields.  There are more of these
    //* than there are contin_specs.
    size_t n_contin_fields() const
    {
        return _contin.size();
    }

    //* number of raw "term algebra" fields.  There are more of
    //* these than there are term_specs.
    size_t n_term_fields() const
    {
        return _n_term_fields;
    }

    /// Given an index into the term_spec array, this returns an
    /// index into the (raw) field array.
    size_t term_to_raw_idx(size_t idx) const
    {
        // @todo: compute at the start in _fields - could be faster..
        size_t raw_idx = 0;
        for (std::vector<term_spec>::const_iterator it = _term.begin();
                it != _term.begin() + idx; ++it)
            raw_idx += it->depth;
        return raw_idx;
    }

    /// Given an index into the 'raw' field array, this returns an
    /// index into the corresponding disc_spec array.
    ///
    /// If the @raw_idx does not correspond to any disc_spec, then an
    /// OC_ASSERT is raised.
    size_t raw_to_disc_idx(size_t raw_idx) const
    {
        size_t begin_disc_idx = begin_disc_raw_idx();
        size_t end_disc_idx = end_disc_raw_idx();

        // @todo: compute at the start in _fields - could be faster..
        OC_ASSERT(raw_idx >= begin_disc_idx &&
                  raw_idx < end_disc_idx);

        // There's exactly one disc_spec per disc field.
        return raw_idx - begin_disc_idx;
    }

protected:

    // _fields holds all of the raw fields in one array.
    // They are arranged in order, so that the "term algebra" fields
    // come first, followed by the continuous fields, then the
    // (multi-bit) discrete fields (i.e. the discrete fields that
    // require more than one bit), and finally the one-bit or boolean
    // fields.
    //
    // The locations and sizes of these can be gotten using the methods:
    // begin_term_fields() - end_term_fields()
    // begin_disc_fields() - end_disc_fields()
    // begin_bit_fields() - end_bit_fields()
    std::vector<field> _fields;
    std::vector<term_spec> _term;
    std::vector<contin_spec> _contin;
    std::vector<disc_spec> _disc; // Includes bits.
    size_t _nbool; // the number of disc_spec that requires only 1 bit to pack

    // Cached values for start location of the continuous and discrete
    // fields in the _fields array.  We don't need to cache the term
    // start, as that is same as start of _field. Meanwhile, the start
    // of the booleans is just _nbool back from the end of the array,
    // so we don't cache that either. These can be computed from
    // scratch (below, with compute_starts()) and are cached here for
    // performance reasons.
    field_iterator _contin_start, _disc_start;
    size_t _end_term_raw_idx;
    size_t _begin_disc_raw_idx;
    size_t _end_disc_raw_idx;
    size_t _begin_bit_raw_idx;
    size_t _end_bit_raw_idx;

    size_t _n_disc_fields;
    size_t _n_term_fields;

    // Figure out where, in the field array, the varous different
    // raw field types start. Cache these, as they're handy to have around.
    void compute_starts()
    {
        _disc_start = _fields.begin();
        for (const term_spec& o : _term)
            _disc_start += o.depth;

        field_iterator term_start = _fields.begin();
        _end_term_raw_idx     = distance(term_start, end_term_fields());

        _begin_disc_raw_idx   = distance(term_start, begin_disc_fields());
        _end_disc_raw_idx     = distance(term_start, end_disc_fields());

        _begin_bit_raw_idx    = distance(term_start, begin_bit_fields());
        _end_bit_raw_idx      = distance(term_start, end_bit_fields());

        _n_disc_fields   = distance(begin_disc_fields(), end_disc_fields());
        _n_term_fields   = distance(begin_term_fields(), end_term_fields());

    }

    size_t back_offset() const
    {
        return _fields.empty() ? 0 :
               _fields.back().major_offset*bits_per_packed_t +
               _fields.back().minor_offset + _fields.back().width;
    }

    //* Build spec s, n times, that is:
    // Fill the corresponding field in _fields, n times.
    // Add the spec n times in _term, _contin or _disc depending on its type.
    void build_spec(const spec& s, size_t n);

    //* Build term_spec os, n times
    // Fill the corresponding field, n times.
    // Add the spec in _term, n times.
    void build_term_spec(const term_spec& os, size_t n);

    // Like above but for disc, and also,
    // increment _nbool by n if ds has multiplicity 2 (i.e. only needs one bit).
    void build_disc_spec(const disc_spec& ds, size_t n);

    void build_contin_spec(const contin_spec& cs, size_t n);

    template<typename Self, typename Iterator>
    struct bit_iterator_base
        : boost::random_access_iterator_helper<Self, bool>
    {
        typedef std::ptrdiff_t Distance;

        Self& operator++()
        {
            _mask <<= 1;
            if (!_mask) {
                _mask = packed_t(1);
                ++_it;
            }
            return (*((Self*)this));
        }

        Self& operator--()
        {
            static const packed_t reset = packed_t(1 << (bits_per_packed_t - 1));
            _mask >>= 1;
            if (!_mask) {
                _mask = reset;
                --_it;
            }
            return (*((Self*)this));
        }

        Self& operator+=(Distance n)
        {
            if (n < 0)
                return (*this) -= (-n);
            _it += n / bits_per_packed_t;
            dorepeat(n % bits_per_packed_t) //could be faster...
                ++(*this);
            return (*((Self*)this));
        }

        Self& operator-=(Distance n)
        {
            if (n < 0)
                return (*this) += (-n);
            _it -= n / bits_per_packed_t;
            dorepeat(n % bits_per_packed_t) //could be faster...
                --(*this);
            return (*((Self*)this));
        }

        bool operator<(const Self& x) const
        {
            return (_it < x._it ? true : integer_log2(_mask) < integer_log2(x._mask));
        }

        friend Distance operator-(const Self& x, const Self& y)
        {
            return (bits_per_packed_t*(x._it - y._it) +
                    integer_log2(x._mask) - integer_log2(y._mask));
        }

        bool operator==(const Self& rhs) const
        {
            return (_it == rhs._it && _mask == rhs._mask);
        }

    protected:
        bit_iterator_base(Iterator it, width_t offset)
            : _it(it), _mask(packed_t(1) << offset) { }
        bit_iterator_base(packed_t mask, Iterator it) : _it(it), _mask(mask) { }
        bit_iterator_base() : _it(), _mask(0) { }

        Iterator _it; // instance iterator
        packed_t _mask; // mask over the packed_t pointed by _it,
                        // i.e. the data of interest
    };

    template<typename Iterator, typename Value>
    struct iterator_base
        : boost::random_access_iterator_helper<Iterator, Value>
    {
        typedef std::ptrdiff_t Distance;

        struct reference
        {
            reference(const Iterator* it, size_t idx) : _it(it), _idx(idx) { }

            operator Value() const {
                return do_get();
            }

            reference& operator=(Value x) {
                do_set(x); return *this;
            }
            reference& operator=(const reference& rhs) {
                do_set(rhs);
                return *this;
            }

            reference& operator+=(Value x) {
                do_set(do_get() + x);   return *this;
            }
            reference& operator-=(Value x) {
                do_set(do_get() - x); return *this;
            }
            reference& operator*=(Value x) {
                do_set(do_get()*x);  return *this;
            }
            reference& operator/=(Value x) {
                do_set(do_get() / x); return *this;
            }
        protected:
            const Iterator* _it;
            size_t _idx;

            Value do_get() const;
            void do_set(Value x);
        };

        Iterator& operator++() {
            ++_idx;
            return (*((Iterator*)this));
        }
        Iterator& operator--() {
            --_idx;
            return (*((Iterator*)this));
        }
        Iterator& operator+=(Distance n) {
            _idx += n;
            return (*((Iterator*)this));
        }
        Iterator& operator-=(Distance n) {
            _idx -= n;
            return (*((Iterator*)this));
        }
        bool operator<(const Iterator& x) const {
            return (_idx < x._idx);
        }
        friend Distance operator-(const Iterator& x, const Iterator& y) {
            return (x._idx -y._idx);
        }

        bool operator==(const Iterator& rhs) const {
            return (_idx == rhs._idx);
        }

        int idx() const {
            return _idx;
        }
    protected:
        iterator_base(const field_set& fs, size_t idx) : _fs(&fs), _idx(idx) { }
        iterator_base() : _fs(NULL), _idx(0) { }

        const field_set* _fs;
        size_t _idx;
    };
public:
    // --------------------------------------------------------
    struct bit_iterator
        : public bit_iterator_base<bit_iterator, packed_vec::iterator>
    {
        friend struct field_set;

        struct reference
        {
            reference(packed_vec::iterator it, packed_t mask)
                : _it(it), _mask(mask) {}

            operator bool() const {
                return (*_it & _mask) != 0;
            }

            bool operator~() const {
                return (*_it & _mask) == 0;
            }
            reference& flip() {
                do_flip(); return *this;
            }

            reference& operator=(bool x) {
                do_assign(x); return *this;
            }
            reference& operator=(const reference& rhs) {
                do_assign(rhs);
                return *this;
            }

            reference& operator|=(bool x) {
                if  (x) do_set();   return *this;
            }
            reference& operator&=(bool x) {
                if (!x) do_reset(); return *this;
            }
            reference& operator^=(bool x) {
                if  (x) do_flip();  return *this;
            }
            reference& operator-=(bool x) {
                if  (x) do_reset(); return *this;
            }
        protected:
            packed_vec::iterator _it;
            packed_t _mask;

            void do_set() {
                *_it |= _mask;
            }
            void do_reset() {
                *_it &= ~_mask;
            }
            void do_flip() {
                *_it ^= _mask;
            }
            void do_assign(bool x) {
                x ? do_set() : do_reset();
            }
        };

        reference operator*() const {
            return reference(_it, _mask);
        }
        friend class const_bit_iterator;

        bit_iterator() { }
    protected:
        bit_iterator(packed_vec::iterator it, width_t offset)
            : bit_iterator_base<bit_iterator, packed_vec::iterator>(it, offset)
        { }
    };

    struct const_bit_iterator
        : public bit_iterator_base<const_bit_iterator, packed_vec::const_iterator>
    {
        friend class field_set;
        bool operator*() const {
            return (*_it & _mask) != 0;
        }
        const_bit_iterator(const bit_iterator& bi)
            : bit_iterator_base < const_bit_iterator,
                                  packed_vec::const_iterator > (bi._mask, bi._it) { }

        const_bit_iterator() { }
    protected:
        const_bit_iterator(packed_vec::const_iterator it, width_t offset)
            : bit_iterator_base < const_bit_iterator,
                                  packed_vec::const_iterator > (it, offset) { }
    };

    // --------------------------------------------------------
    struct disc_iterator : public iterator_base<disc_iterator, disc_t>
    {
        friend struct field_set;
        friend struct reference;
        friend class const_disc_iterator;

        reference operator*() const
        {
            return reference(this, _idx);
        }

        disc_iterator() : _inst(NULL) { }

        // For convenience.
        multiplicity_t multy() const
        {
            // The _idx is raw; we want the index into the disc_spec.
            size_t spec_idx = _fs->raw_to_disc_idx(_idx);
            return _fs->disc_and_bit()[spec_idx].multy;
        }

        void randomize(opencog::RandGen& rng = randGen())
        {
            _fs->set_raw(*_inst, _idx, rng.randint(multy()));
        }

    protected:
        disc_iterator(const field_set& fs, size_t idx, packed_vec& inst)
            : iterator_base<disc_iterator, disc_t>(fs, idx), _inst(&inst) { }
        packed_vec* _inst;
    };

    struct const_disc_iterator
        : public iterator_base<const_disc_iterator, disc_t>
    {
        friend class field_set;
        disc_t operator*() const
        {
            return _fs->get_raw(*_inst, _idx);
        }

        const_disc_iterator(const disc_iterator& bi) :
            iterator_base<const_disc_iterator, disc_t>(*bi._fs, bi._idx),
            _inst(bi._inst) { }

        const_disc_iterator() : _inst(NULL) { }

        // For convenience.
        multiplicity_t multy() const
        {
            // The _idx is raw; we want the index into the disc_spec.
            size_t spec_idx = _fs->raw_to_disc_idx(_idx);
            return _fs->disc_and_bit()[spec_idx].multy;
        }

    protected:
        const_disc_iterator(const field_set& fs, size_t idx, const packed_vec& inst)
            : iterator_base<const_disc_iterator, disc_t>(fs, idx), _inst(&inst) { }
        const packed_vec* _inst;
    };

    // --------------------------------------------------------
    struct contin_iterator : public iterator_base<contin_iterator, contin_t>
    {
        friend struct field_set;
        friend struct reference;
        friend class const_contin_iterator;

        reference operator*() const
        {
            return reference(this, _idx);
        }

        // For convenience.
        const contin_spec& spec(){
            return _fs->contin()[_idx];
        }

        contin_iterator() : _inst(NULL) { }

    protected:
        contin_iterator(const field_set& fs, size_t idx, contin_vec& inst)
            : iterator_base<contin_iterator, contin_t>(fs, idx), _inst(&inst)
        { }
        contin_vec* _inst;
    };

    struct const_contin_iterator
        : public iterator_base<const_contin_iterator, contin_t>
    {
        friend class field_set;

        contin_t operator*() const
        {
            return _inst->at(_idx);
        }

        // For convenience.
        const contin_spec& spec()
        {
            return _fs->contin()[_idx];
        }

        const_contin_iterator(const contin_iterator& bi)
            : iterator_base<const_contin_iterator, contin_t>(*bi._fs, bi._idx),
              _inst(bi._inst) { }

        const_contin_iterator() : _inst(NULL) { }

    protected:
        const_contin_iterator(const field_set& fs, size_t idx,
                const contin_vec& inst)
            : iterator_base<const_contin_iterator, contin_t>(fs, idx),
              _inst(&inst) { }
        const contin_vec* _inst;
    };

    // --------------------------------------------------------
    struct term_iterator
        : public iterator_base<term_iterator, term_t>
    {
        friend class field_set;
        friend struct reference;
        friend class const_term_iterator;


        reference operator*() const
        {
            return reference(this, _idx);
        }

        term_iterator() : _inst(NULL) { }

    protected:
        term_iterator(const field_set& fs, size_t idx, packed_vec& inst)
            : iterator_base<term_iterator, term_t>(fs, idx),
              _inst(&inst) { }

        packed_vec* _inst;
    };

    struct const_term_iterator
        : public iterator_base<const_term_iterator, term_t>
    {
        friend class field_set;

        const term_t& operator*() const
        {
            return _fs->get_term(*_inst, _idx);
        }

        const_term_iterator(const term_iterator& bi) :
            iterator_base<const_term_iterator, term_t>(*bi._fs, bi._idx),
            _inst(bi._inst) { }

        const_term_iterator() : _inst(NULL) { }

    protected:
        const_term_iterator(const field_set& fs, size_t idx,
                    const packed_vec& inst)
            : iterator_base<const_term_iterator, term_t>(fs, idx),
            _inst(&inst) { }
        const packed_vec* _inst;
    };

    // --------------------------------------------------------
    // Get the begin, end iterators for the bit fields.
    /// @todo rename that begin_bit for more consistency
    const_bit_iterator begin_bit(const instance& inst) const
    {
        return (begin_bit_fields() == _fields.end() ? const_bit_iterator() :
                const_bit_iterator(inst._bit_disc.begin() + begin_bit_fields()->major_offset,
                                   begin_bit_fields()->minor_offset));
    }

    const_bit_iterator end_bit(const instance& inst) const
    {
        return (begin_bit_fields() == _fields.end() ? const_bit_iterator() :
                ++const_bit_iterator(--inst._bit_disc.end(), _fields.back().minor_offset));
    }

    bit_iterator begin_bit(instance& inst) const
    {
        return (begin_bit_fields() == _fields.end() ? bit_iterator() :
                bit_iterator(inst._bit_disc.begin() + begin_bit_fields()->major_offset,
                             begin_bit_fields()->minor_offset));
    }

    bit_iterator end_bit(instance& inst) const
    {
        return (begin_bit_fields() == _fields.end() ? bit_iterator() :
                ++bit_iterator(--inst._bit_disc.end(), _fields.back().minor_offset));
    }

    // ------------------------------------------------
    // Get the begin, end iterators for the disc fields.
    const_disc_iterator begin_disc(const instance& inst) const
    {
        return const_disc_iterator(*this, begin_disc_raw_idx(), inst._bit_disc);
    }

    const_disc_iterator end_disc(const instance& inst) const
    {
        return const_disc_iterator(*this, end_disc_raw_idx(), inst._bit_disc);
    }

    disc_iterator begin_disc(instance& inst) const {
        return disc_iterator(*this, begin_disc_raw_idx(), inst._bit_disc);
    }

    disc_iterator end_disc(instance& inst) const {
        return disc_iterator(*this, end_disc_raw_idx(), inst._bit_disc);
    }

    // --------------------------------------------------
    // Get the begin, end iterators for the contin fields.
    const_contin_iterator begin_contin(const instance& inst) const {
        return const_contin_iterator(*this, 0, inst._contin);
    }

    const_contin_iterator end_contin(const instance& inst) const {
        return const_contin_iterator(*this, inst._contin.size(), inst._contin);
    }

    contin_iterator begin_contin(instance& inst) const {
        return contin_iterator(*this, 0, inst._contin);
    }

    contin_iterator end_contin(instance& inst) const {
        return contin_iterator(*this, _contin.size(), inst._contin);
    }

    // ------------------------------------------------
    // Get the begin, end iterators for the term fields.
    const_term_iterator begin_term(const instance& inst) const {
        return const_term_iterator(*this, 0, inst._bit_disc);
    }

    const_term_iterator end_term(const instance& inst) const {
        return const_term_iterator(*this, _term.size(), inst._bit_disc);
    }

    term_iterator begin_term(instance& inst) const {
        return term_iterator(*this, 0, inst._bit_disc);
    }

    term_iterator end_term(instance& inst) const {
        return term_iterator(*this, _term.size(), inst._bit_disc);
    }

    // ------------------------------------------------
    // Get the begin, end iterators for all of the raw fields.
    const_disc_iterator begin_raw(const instance& inst) const {
        return const_disc_iterator(*this, 0, inst._bit_disc);
    }
    const_disc_iterator end_raw(const instance& inst) const {
        return const_disc_iterator(*this, _fields.size(), inst._bit_disc);
    }
    disc_iterator begin_raw(instance& inst) const {
        return disc_iterator(*this, 0, inst._bit_disc);
    }
    disc_iterator end_raw(instance& inst) const {
        return disc_iterator(*this, _fields.size(), inst._bit_disc);
    }

    // Help print out the field set.
    std::ostream& ostream_field_set(std::ostream& out) const;
};

template<>
inline disc_t field_set::iterator_base < field_set::disc_iterator,
disc_t >::reference::do_get() const
{
    return _it->_fs->get_raw(*_it->_inst, _idx);
}

template<>
inline void field_set::iterator_base < field_set::disc_iterator,
disc_t >::reference::do_set(disc_t x)
{
    _it->_fs->set_raw(*_it->_inst, _idx, x);
}

template<>
inline contin_t field_set::iterator_base < field_set::contin_iterator,
contin_t >::reference::do_get() const
{
    return _it->_inst->at(_idx);
}

template<>
inline void field_set::iterator_base < field_set::contin_iterator,
contin_t >::reference::do_set(contin_t x)
{
    _it->_inst->at(_idx) = x;
}

/// pack the data in [from,from+dof) according to our scheme, copy to out
///
/// 'It' is assumed to be an iterator over values (e.g. disc_t's, which
/// are unsigned ints).
/// 'Out' is assumed to be an iterator pointing at an instance.  Recall
/// that the instance is a vector of packed_t, which may be 32 or 64-bit.
//
template<typename It, typename Out>
Out field_set::pack(It from, Out out) const
{
    unsigned int offset = 0;

    for (const term_spec& o : _term) {
        size_t width = nbits_to_pack(o.branching);
        size_t total_width = size_t((width * o.depth - 1) /
                                    bits_per_packed_t + 1) * bits_per_packed_t;
        dorepeat (o.depth) {
            *out |= packed_t(*from++) << offset;
            offset += width;
            if (offset == bits_per_packed_t) {
                offset = 0;
                ++out;
            }
        }
        offset += total_width - (o.depth * width); //term vars must pack evenly
        if (offset == bits_per_packed_t) {
            offset = 0;
            ++out;
        }
    }

    for (const disc_spec& d : _disc) {
        *out |= packed_t(*from++) << offset;
        offset += nbits_to_pack(d.multy);
        if (offset == bits_per_packed_t) {
            offset = 0;
            ++out;
        }
    }
    if (offset > 0) //so we always point to one-past-the-end
        ++out;
    return out;
}

inline std::ostream& operator<<(std::ostream& out,
                                const field_set& fs)
{
    return fs.ostream_field_set(out);
}


} // ~namespace moses
} // ~namespace opencog

#endif
