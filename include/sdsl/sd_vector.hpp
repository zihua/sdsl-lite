/* sdsl - succinct data structures library
    Copyright (C) 2012 Simon Gog

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see http://www.gnu.org/licenses/ .
*/
/*!\file sd_vector.hpp
   \brief sd_vector.hpp contains the sdsl::sd_vector class, and
          classes which support rank and select for sd_vector.
   \author Simon Gog
*/
#ifndef SDSL_SD_VECTOR
#define SDSL_SD_VECTOR

#include "int_vector.hpp"
#include "select_support_mcl.hpp"
#include "util.hpp"
#include "bitmagic.hpp"

//! Namespace for the succinct data structure library
namespace sdsl
{

// forward declaration needed for friend declaration
template<class hi_bit_vector_type=bit_vector, class Select1Support=select_support_mcl<1>, class Select0Support=select_support_mcl<0> >
class sd_rank_support;  // in sd_vector

// forward declaration needed for friend declaration
template<class hi_bit_vector_type=bit_vector, class Select1Support=select_support_mcl<1>, class Select0Support=select_support_mcl<0> >
class sd_select_support;  // in sd_vector

//! A bit vector which compresses very sparse populated bit vectors by
// representing the positions of 1 by the Elias-Fano representation for non-decreasing sequences
/*!
 *
 * \par Other implementations of this data structure:
 *  - the sdarray of Okanohara and Sadakane
 *  - Sebastiano Vigna implemented a elias_fano class in this sux library.
 *
 * \par References
 *  - P. Elias: ,,Efficient storage and retrieval by content and address of static files'',
 *              Journal of the ACM, 1974
 *  - R. Fano: ,,On the number of bits required to implement an associative memory''.
 *             Memorandum 61. Computer Structures Group, Project MAC, MIT, 1971
 *  - D. Okanohara, K. Sadakane: ,,Practical Entropy-Compressed Rank/Select Dictionary'',
 *             Proceedings of ALENEX 2007.
 */
template<class hi_bit_vector_type=bit_vector, class hi_select_1=select_support_mcl<1>, class hi_select_0=select_support_mcl<0> >
class sd_vector
{
    public:
        typedef bit_vector::size_type size_type;
        typedef size_type value_type;
        typedef hi_select_0 select_0_support_type;
        typedef hi_select_1 select_1_support_type;

        friend class sd_rank_support<hi_bit_vector_type, select_1_support_type, select_0_support_type>;
        friend class sd_select_support<hi_bit_vector_type, select_1_support_type, select_0_support_type>;

        typedef sd_rank_support<hi_bit_vector_type, select_1_support_type, select_0_support_type> rank_1_type;
        typedef sd_select_support<hi_bit_vector_type, select_1_support_type, select_0_support_type> select_1_type;
    private:
        // we need this variables to represent the m ones of the original bit vector of size n
        size_type m_size;		 // length of the original bit vector
        uint8_t   m_wl;			 // log n - log m, where n is the length of the original bit vector
        // and m is the number of ones in the bit vector, lw is the abbreviation
        // for ,,with (of) low (part)''

        int_vector<> m_low;      // vector for the least significant bits of the positions of the m ones
        hi_bit_vector_type   	m_high;     // bit vector that represents the most significant bit in permuted order
        select_1_support_type 	m_high_1_select; //
        select_0_support_type 	m_high_0_select; //

        void copy(const sd_vector& v) {
            m_size = v.m_size;
            m_wl   = v.m_wl;
            m_low  = v.m_low;
            m_high = v.m_high;
            m_high_1_select = v.m_high_1_select;
            m_high_1_select.set_vector(&m_high);
            m_high_0_select = v.m_high_0_select;
            m_high_0_select.set_vector(&m_high);
        }


    public:
        const hi_bit_vector_type& high;
        const int_vector<>& low;
        const select_1_support_type&	 high_1_select;
        const select_0_support_type&	 high_0_select;

        sd_vector():m_size(0), m_wl(0),
            high(m_high), low(m_low),
            high_1_select(m_high_1_select), high_0_select(m_high_0_select) {
        }

        sd_vector(const bit_vector& bv):high(m_high),low(m_low),
            high_1_select(m_high_1_select), high_0_select(m_high_0_select) {
            m_size = bv.size();
            size_type m = util::get_one_bits(bv);
            uint8_t logm = bit_magic::l1BP(m)+1;
            uint8_t logn = bit_magic::l1BP(m_size)+1;
            if (logm == logn) {
                --logm;    // to ensure logn-logm > 0
            }
            m_wl    = logn - logm;
            m_low = int_vector<>(m, 0, m_wl);
            bit_vector high = bit_vector(m + (1ULL<<logm), 0); //
            const uint64_t* bvp = bv.data();
            for (size_type i=0, mm=0,last_high=0,highpos=0; i < (bv.size()+63)/64; ++i, ++bvp) {
                size_type position = 64*i;
                uint64_t  w = *bvp;
                while (w) {
                    uint8_t offset = bit_magic::r1BP(w);
                    w >>= offset;   // note:  w >>= (offset+1) can not be applied for offset=63!
                    position += offset;
                    if (position >= bv.size())
                        break;
                    // (1) handle high part
                    size_type cur_high = position >> m_wl;
                    highpos += (cur_high - last_high);   // write cur_high-last_high 0s
                    last_high = cur_high;
                    // (2) handle low part
                    m_low[mm++] = position; // int_vector truncates the most significant logm bits
                    high[highpos++] = 1;     // write 1 for the entry
                    position += 1;
                    w >>= 1;
                }
            }
            util::assign(m_high, high);
            util::init_support(m_high_1_select, &m_high);
            util::init_support(m_high_0_select, &m_high);
        }

        //! Accessing the i-th element of the original bit_vector
        /*! \param i An index i with \f$ 0 \leq i < size()  \f$.
        *   \return The i-th bit of the original bit_vector
        *   \par Time complexity
        *   		\f$ \Order{t_{select0} + n/m} \f$, where m equals the number of zeros
        *	\par Remark
         *	     The time complexity can be easily improved to
        *            \f$\Order{t_{select0}+\log(n/m)}\f$
        *        by using binary search in the second step.
        */
        value_type operator[](size_type i)const {
//			size_type h = i >> m_wl; // extract high part
            // search for the h's zero in m_high
            //size_type m_high_0_select(  );
            // split problem in two parts:
            // (1) find  >=
            size_type high_val = (i >> (m_wl));
            size_type sel_high = m_high_0_select.select(high_val + 1);
            size_type rank_low = sel_high - high_val;
            if (0 == rank_low)
                return 0;
            size_type val_low = i & bit_magic::Li1Mask[ m_wl ];
            // now since rank_low > 0 => sel_high > 0
            --sel_high; --rank_low;
            while (m_high[sel_high] and m_low[rank_low] > val_low) {
                if (sel_high > 0) {
                    --sel_high; --rank_low;
                } else
                    return 0;
            }
            return m_high[sel_high] and m_low[rank_low] == val_low;
        }

        //! Swap method
        void swap(sd_vector& v) {
            if (this != &v) {
                std::swap(m_size, v.m_size);
                std::swap(m_wl, v.m_wl);
                m_low.swap(v.m_low);
                m_high.swap(v.m_high);
                m_high_1_select.swap(v.m_high_1_select);
                m_high_1_select.set_vector(&m_high);
                v.m_high_1_select.set_vector(&(v.m_high));

                m_high_0_select.swap(v.m_high_0_select);
                m_high_0_select.set_vector(&m_high);
                v.m_high_0_select.set_vector(&(v.m_high));
            }
        }

        //! Returns the size of the original bit vector.
        size_type size()const {
            return m_size;
        }

        sd_vector& operator=(const sd_vector& v) {
//		   std::cout<<"copy constructor of sd_vector was called"<<std::endl;
            if (this != &v) {
                copy(v);
            }
            return *this;
        }

        //! Serializes the data structure into the given ostream
        size_type serialize(std::ostream& out)const {
            size_type written_bytes = 0;
            written_bytes += util::write_member(m_size, out);
            written_bytes += util::write_member(m_wl, out);
            written_bytes += m_low.serialize(out);
            written_bytes += m_high.serialize(out);
            written_bytes += m_high_1_select.serialize(out);
            written_bytes += m_high_0_select.serialize(out);
            return written_bytes;
        }

        //! Loads the data structure from the given istream.
        void load(std::istream& in) {
            util::read_member(m_size, in);
            util::read_member(m_wl, in);
            m_low.load(in);
            m_high.load(in);
            m_high_1_select.load(in, &m_high);
            m_high_0_select.load(in, &m_high);
        }

#ifdef MEM_INFO
        void mem_info(std::string label="")const {
            if (label=="")
                label = "sd_vector";
            size_type bytes = util::get_size_in_bytes(*this);
            std::cout << "list(label=\""<<label<<"\", size = "<< bytes/(1024.0*1024.0) << ",\n";
            m_high.mem_info("high part"); std::cout << ",\n";
            m_low.mem_info("low part"); std::cout << ",\n";
            m_high_1_select.mem_info("high_1_select"); std::cout << ",\n";
            m_high_0_select.mem_info("high_0_select"); std::cout << ")\n";
        }
#endif

};

template<class hi_bit_vector_type, class Select1Support, class Select0Support>
class sd_rank_support
{
    public:
        typedef bit_vector::size_type size_type;
        typedef sd_vector<hi_bit_vector_type, Select1Support, Select0Support> bit_vector_type;
    private:
        const bit_vector_type* m_v;

    public:

        explicit sd_rank_support(const bit_vector_type* v=NULL) {
            init(v);
        }

        void init(const bit_vector_type* v=NULL) {
            set_vector(v);
        }

        size_type rank(size_type i)const {
            // split problem in two parts:
            // (1) find  >=
            size_type high_val = (i >> (m_v->m_wl));
//std::cout<<"high_val="<<high_val<<" i="<<i<<" / "<<m_v->m_high.size()<<" / "<<m_v->size() <<std::endl;
//std::cout<<"m_v="<<m_v<<std::endl;
//std::cout<<"m_v="<< (&(m_v->m_high_0_select)) <<std::endl;
//for(size_type j=1; j<2; ++j)
//	std::cout<<"m_v->m_high_0_select("<<j<<")="<< m_v->m_high_0_select.select(j) << std::endl;
//std::cout<<"------------"<<std::endl;
            size_type sel_high = m_v->m_high_0_select.select(high_val + 1);
//std::cout<<"sel_high="<<sel_high<<" /"<<m_v->m_high.size() <<std::endl;
            size_type rank_low = sel_high - high_val; //
//std::cout<<"rank_low="<<rank_low<<std::endl;
            if (0 == rank_low)
                return 0;
            size_type val_low = i & bit_magic::Li1Mask[ m_v->m_wl ];
//std::cout<<"val_low="<<val_low<<std::endl;
            // now since rank_low > 0 => sel_high > 0
            do {
                if (!sel_high)
                    return 0;
                --sel_high; --rank_low;
            } while (m_v->m_high[sel_high] and m_v->m_low[rank_low] >= val_low);
//std::cout<<"result = "<<rank_low+1<<std::endl;
            return rank_low+1;
        }

        const size_type operator()(size_type i)const {
            return rank(i);
        }

        const size_type size()const {
            return m_v->size();
        }

//		void set_vector(const bit_vector_type *v=NULL){
        void set_vector(const bit_vector_type* v=NULL) {
            m_v = v;
        }

        sd_rank_support& operator=(const sd_rank_support& rs) {
            if (this != &rs) {
                set_vector(rs.m_v);
            }
            return *this;
        }

        void swap(sd_rank_support&) { }

        bool operator==(const sd_rank_support& ss)const {
            if (this == &ss)
                return true;
            return ss.m_v == m_v;
        }

        bool operator!=(const sd_rank_support& rs)const {
            return !(*this == rs);
        }


        void load(std::istream& in, const bit_vector_type* v=NULL) {
            set_vector(v);
        }

        size_type serialize(std::ostream& out)const {
            size_type written_bytes = 0;
            return written_bytes;
        }

#ifdef MEM_INFO
        void mem_info(std::string label="")const {
            if (label=="")
                label = "sd_rank_support";
            size_type bytes = util::get_size_in_bytes(*this);
            std::cout << "list(label=\""<<label<<"\", size = "<< bytes/(1024.0*1024.0) << ")\n";
        }
#endif

};

template<class hi_bit_vector_type, class Select1Support, class Select0Support>
class sd_select_support
{
    public:
        typedef bit_vector::size_type size_type;
        typedef sd_vector<hi_bit_vector_type, Select1Support, Select0Support> bit_vector_type;
    private:
        const bit_vector_type* m_v;

    public:

        explicit sd_select_support(const bit_vector_type* v=NULL) {
            init(v);
        }

        void init(const bit_vector_type* v=NULL) {
            set_vector(v);
        }

        //! Returns the position of the i-th occurrence in the bit vector.
        size_type select(size_type i)const {
            return m_v->m_low[i-1] +  // lower part of the number
                   ((m_v->m_high_1_select(i) + 1 - i)  << (m_v->m_wl));  // upper part
            //^-number of 0 before the i-th 1-^      ^-shift by m_wl
        }

        const size_type operator()(size_type i)const {
            return select(i);
        }

        const size_type size()const {
            return m_v->size();
        }

        void set_vector(const bit_vector_type* v=NULL) {
            m_v = v;
        }

        sd_select_support& operator=(const sd_select_support& rs) {
            if (this != &rs) {
                set_vector(rs.m_v);
            }
            return *this;
        }

        void swap(sd_select_support&) { }

        bool operator==(const sd_select_support& ss)const {
            if (this == &ss)
                return true;
            return ss.m_v == m_v;
        }

        bool operator!=(const sd_select_support& rs)const {
            return !(*this == rs);
        }


        void load(std::istream& in, const bit_vector_type* v=NULL) {
            set_vector(v);
        }

        size_type serialize(std::ostream& out)const {
            size_type written_bytes = 0;
            return written_bytes;
        }

#ifdef MEM_INFO
        void mem_info(std::string label="")const {
            if (label=="")
                label = "sd_select_support";
            size_type bytes = util::get_size_in_bytes(*this);
            std::cout << "list(label=\""<<label<<"\", size = "<< bytes/(1024.0*1024.0) << ")\n";
        }
#endif

};

} // end namespace

#endif
