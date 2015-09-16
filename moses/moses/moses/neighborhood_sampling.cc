/** neighborhood_sampling.cc ---
 *
 * Copyright (C) 2015 OpenCog Foundation
 *
 * Author: Moshe Looks, Nil Geisweiller, Xiaohui Liu
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

#include "neighborhood_sampling.h"

namespace opencog { namespace moses {

// See header for comment
void generate_contin_neighbor(const field_set& fs,
                              instance& inst,
                              field_set::contin_iterator it,
                              unsigned dist,
                              opencog::RandGen& rng)
{
    if(dist <= 0)
        return;
    *it = it.spec().get_new(*it, dist, rng);
}

// See header for comment
size_t safe_binomial_coefficient(unsigned k, unsigned n)
{
    size_t res;
    double noi_db = boost::math::binomial_coefficient<double>(k, n);
    try {
        res = boost::numeric_cast<size_t>(noi_db);
    } catch (boost::numeric::positive_overflow&) {
        res = std::numeric_limits<size_t>::max();
    }
    return res;
}

// See header for comment
size_t count_neighborhood_size_from_index(const field_set& fs,
                                          const instance& inst,
                                          unsigned dist,
                                          unsigned starting_index,
                                          size_t max_count)
{
    if (dist == 0)
        return 1;

    size_t number_of_instances = 0;

    // terms
    if ((fs.begin_term_raw_idx() <= starting_index) &&
        (starting_index < fs.end_term_raw_idx()))
    {
        // @todo: handle term algebras
        number_of_instances =
            count_neighborhood_size_from_index(fs, inst, dist,
                                               starting_index
                                               + fs.end_term_raw_idx(),
                                               max_count);
    }
    // discs
    else
    if ((fs.begin_disc_raw_idx() <= starting_index) &&
        (starting_index < fs.end_disc_raw_idx()))
    {
        // Recursive call, moved for one position.
        number_of_instances =
            count_neighborhood_size_from_index(fs, inst, dist,
                                             starting_index + 1, max_count);

        // stop prematurely if above max_count
        if (number_of_instances > max_count)
            return number_of_instances;

        // count all legal values of the knob
        field_set::const_disc_iterator itd = fs.begin_disc(inst);
        itd += starting_index - fs.begin_disc_raw_idx();

        number_of_instances +=
            (itd.multy() - 1)
            * count_neighborhood_size_from_index(fs, inst, dist - 1,
                                               starting_index + 1, max_count);
    }
    // bits
    else
    if ((fs.begin_bit_raw_idx() <= starting_index) &&
        (starting_index < fs.end_bit_raw_idx()))
    {

        // Since bits all have the same multiplicity (viz. 2), and are
        // the last in the field set, there is no need for recursive call.
        unsigned rb = fs.end_bit_raw_idx() - starting_index;
        if (dist <= rb)
            number_of_instances = safe_binomial_coefficient(rb, dist);
    }
    else
    {
        // Harmless; this recursive algo is designed to over-run by
        // exactly one.
    }

    return number_of_instances;
}

size_t count_contin_neighborhood(const field_set& fs,
                               unsigned dist,
                               size_t number_of_instances,
                               size_t max_count) {
    // Calculate number_of_instances for each possible distance i
    // of the current contin.
    size_t length_bits = fs.n_contin_fields() * dist;
    if(length_bits > 0){
        double dlogsize = std::log2(max_count) - std::log2(number_of_instances);
        if(length_bits > dlogsize)
            return max_count + 1;
        else
            return number_of_instances << length_bits;
    } else
        return number_of_instances;
}
// See header for comment
size_t count_neighborhood_size(const field_set& fs,
                               const instance& inst,
                               unsigned dist,
                               size_t max_count)
{
    // to avoid overflow
    size_t number_of_instances =
        count_neighborhood_size_from_index(fs, inst, dist, 0, max_count);
    if(number_of_instances > max_count)
        return number_of_instances;
    return count_contin_neighborhood(fs, dist, number_of_instances, max_count);

}

// See header for comment
size_t count_neighborhood_size(const field_set& fs,
                               unsigned dist,
                               size_t max_count)
{
    instance inst(fs.packed_width(), fs.n_contin_fields());
    return count_neighborhood_size(fs, inst, dist, max_count);
}

// See header for comment
size_t sample_new_instances(size_t total_number_of_neighbours,
                            size_t number_of_new_instances,
                            size_t current_number_of_instances,
                            const instance& center_inst,
                            instance_set<composite_score>& deme,
                            unsigned dist,
                            marker_vec& changed_contin)
{
    // We assume that the total number of neighbors was just an estimate.
    // If the number of requested new instances is even close to the
    // estimate, then we need an accurate count of the total. We need
    // an accurate count for the case of generating the entire
    // neighborhood (as otherwise, the deme.resize will be bad).
    if (2 * number_of_new_instances > total_number_of_neighbours) {
        total_number_of_neighbours =
                count_neighborhood_size(deme.fields(), center_inst, dist,
                                        number_of_new_instances);
    }

    if (number_of_new_instances < total_number_of_neighbours) {
        // Resize the deme so it can take new instances.
        deme.resize(current_number_of_instances + number_of_new_instances);
        // Sample number_of_new_instances instances at
        // distance 'distance' from the exemplar.
        sample_from_neighborhood(deme.fields(), dist,
                                 number_of_new_instances,
                                 deme.begin() + current_number_of_instances,
                                 deme.end(),
                                 center_inst,
                                 changed_contin,
                                 current_number_of_instances);
    } else {
        number_of_new_instances = total_number_of_neighbours;
        // Resize the deme so it can take new instances
        deme.resize(current_number_of_instances + number_of_new_instances);
        // Add all instances on the distance dist from
        // the initial instance.
        generate_all_in_neighborhood(deme.fields(), dist,
                                     deme.begin() + current_number_of_instances,
                                     deme.end(),
                                     center_inst);
    }
    return number_of_new_instances;
}

// See header for comment
size_t sample_new_instances(size_t number_of_new_instances,
                            size_t current_number_of_instances,
                            const instance& center_inst,
                            instance_set<composite_score>& deme,
                            unsigned dist)
{
    // The number of all neighbours at the distance d (stops
    // counting when above number_of_new_instances).
    size_t total_number_of_neighbours =
        count_neighborhood_size(deme.fields(), center_inst, dist,
                                number_of_new_instances);
    return sample_new_instances(total_number_of_neighbours,
                                number_of_new_instances,
                                current_number_of_instances,
                                center_inst, deme, dist);
}

size_t sample_new_instances(size_t total_number_of_neighbours,
                            size_t number_of_new_instances,
                            size_t current_number_of_instances,
                            const instance& center_inst,
                            instance_set<composite_score>& deme,
                            unsigned dist)
{
    marker_vec changed_contin(0);
    return sample_new_instances(total_number_of_neighbours,
                                number_of_new_instances,
                                current_number_of_instances,
                                center_inst, deme, dist, changed_contin);

}

} // ~namespace moses
} // ~namespace opencog
