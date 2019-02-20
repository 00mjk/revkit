#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <caterpillar/synthesis/lhrs.hpp>
#include <lorina/verilog.hpp>
#include <mockturtle/io/verilog_reader.hpp>
#include <mockturtle/networks/aig.hpp>
#include <mockturtle/networks/mig.hpp>
#include <tweedledum/algorithms/synthesis/dbs.hpp>
#include <tweedledum/algorithms/synthesis/diagonal_synth.hpp>
#include <tweedledum/algorithms/synthesis/gray_synth.hpp>
#include <tweedledum/algorithms/synthesis/stg.hpp>
#include <tweedledum/algorithms/synthesis/tbs.hpp>

#include "types.hpp"

namespace py = pybind11;

namespace revkit
{

template<class LogicNetwork>
std::pair<netlist_t, std::unordered_map<std::string, std::vector<uint32_t>>>
_lhrs_wrapper( std::string const& filename )
{
  LogicNetwork ntk;

  lorina::read_verilog( filename, mockturtle::verilog_reader( ntk ) );

  netlist_t circ;
  caterpillar::logic_network_synthesis_stats st;
  caterpillar::logic_network_synthesis( circ, ntk, {}, {}, &st );

  std::unordered_map<std::string, std::vector<uint32_t>> stats;
  stats["input_indexes"] = st.i_indexes;
  stats["output_indexes"] = st.o_indexes;

  return std::make_pair( circ, stats );
}

void synthesis( py::module m )
{
  using namespace py::literals;

  m.def( "gray_synth", []( py::args parity_terms ) {
    uint32_t num_vars = 0u;

    tweedledum::parity_terms parities;
    for ( auto const& entry : parity_terms )
    {
      auto const& tuple = entry.cast<py::tuple>();
      std::string term = tuple[0].cast<py::str>();
      double angle = tuple[1].cast<py::float_>();

      if ( num_vars == 0u )
      {
        num_vars = term.size();
      }

      uint32_t iterm{0u};
      for ( auto i = 0u; i < term.size(); ++i )
      {
        if ( term[i] == '1' )
        {
          iterm |= 1 << i;
        }
      }
      parities.add_term( iterm, tweedledum::angle( angle ) );
    }
    return tweedledum::gray_synth<netlist_t>( num_vars, parities );
  },
         R"doc(
    GraySynth synthesis algorithm for parity terms

    :param List[(str,float)] args: A list of tuples of parity terms.
        The first entry of the term is a bitstring where the first bit
        corresponds to the first qubit and is 1 if it is contained in the parity
        term. The second parameter is the angle that should be applied for this
        term.
    :rtype: netlist

    The following example synthesizes a controlled S operation::

        from revkit import gray_synth
        from math import pi

        circ = gray_synth(("01", pi / 4), ("10", pi / 4), ("11", -pi / 4))

    .. seealso:: `tweedledum documentation for gray_synth <https://tweedledum.readthedocs.io/en/latest/algorithms/synthesis/gray_synth.html>`_
)doc" );

  enum class oracle_synth_type
  {
    pkrm,
    pprm,
    spectrum
  };

  py::enum_<oracle_synth_type>( m, "oracle_synth_type", "Oracle synthesis kind enumeration" )
      .value( "pkrm", oracle_synth_type::pkrm )
      .value( "pprm", oracle_synth_type::pprm )
      .value( "spectrum", oracle_synth_type::spectrum )
      .export_values();

  m.def( "oracle_synth", []( truth_table_t const& function, oracle_synth_type kind ) {
    netlist_t circ;
    for ( auto i = 0u; i < function.num_vars() + 1u; ++i )
    {
      circ.add_qubit();
    }
    std::vector<tweedledum::qubit_id> qubits( function.num_vars() + 1u );
    std::iota( qubits.begin(), qubits.end(), 0u );

    switch ( kind )
    {
    default:
    case oracle_synth_type::spectrum:
      tweedledum::stg_from_spectrum()( circ, qubits, function );
      break;
    case oracle_synth_type::pkrm:
      tweedledum::stg_from_pkrm()( circ, qubits, function );
      break;
    case oracle_synth_type::pprm:
      tweedledum::stg_from_pprm()( circ, qubits, function );
      break;
    }

    return circ;
  },
         R"doc(
    Oracle synthesis

    Creates a quantum circuit that flips the target qubit based on a Boolean
    function.  The target qubit is the last qubit in the circuit.

    :param truth_table function: Oracle function
    :param kind: Synthesis type
    :rtype: netlist
)doc",
         "function"_a, "kind"_a = oracle_synth_type::spectrum );

  m.def( "diagonal_synth", [&]( std::vector<double> const& angles ) {
    return tweedledum::diagonal_synth<netlist_t>( angles );
  }, R"doc(
    Diagonal unitary synthesis

    Creates a quantum circuit for a diagonal unitary
    :math:`\text{diag}(1, e^{-i\theta_1}, \dots, e^{-i\theta_{2^n-1}})`
    where the input parameters provides the angles :math:`\theta_1, \dots, \theta_{2^n-1}`.

    :param List[float] angles: List of :math:`2^n - 1` angles
    :rtype: netlist

    .. seealso:: `tweedledum documentation for diagonal_synth <https://tweedledum.readthedocs.io/en/latest/algorithms/synthesis/diagonal_synth.html>`_
)doc" );

  m.def( "dbs", []( std::vector<uint32_t> const& perm, oracle_synth_type kind ) {
    switch ( kind )
    {
    default:
    case oracle_synth_type::spectrum:
      return tweedledum::dbs<netlist_t>( perm, tweedledum::stg_from_spectrum() );
    case oracle_synth_type::pkrm:
      return tweedledum::dbs<netlist_t>( perm, tweedledum::stg_from_pkrm() );
    case oracle_synth_type::pprm:
      return tweedledum::dbs<netlist_t>( perm, tweedledum::stg_from_pprm() );
    }
  },
         R"doc(
    Decomposition-based synthesis

    :param List[int] perm: A permutation of the values :math:`\{0, \dots, 2^n - 1\}`.
    :param kind: Synthesis type
    :rtype: netlist

    .. seealso:: `tweedledum documentation for dbs <https://tweedledum.readthedocs.io/en/latest/algorithms/synthesis/dbs.html>`_
)doc",
         "perm"_a, "kind"_a = oracle_synth_type::spectrum );

  m.def( "tbs", []( std::vector<uint32_t> const& perm ) { return tweedledum::tbs<netlist_t>( perm ); }, R"doc(
    Transformation based synthesis

    :param List[int] perm: A permutation of the values :math:`\{0, \dots, 2^n - 1\}`.
    :rtype: netlist

    .. seealso:: `tweedledum documentation for tbs <https://tweedledum.readthedocs.io/en/latest/algorithms/synthesis/tbs.html>`_
)doc",
         "perm"_a );

  m.def( "lhrs", []( std::string const& filename ) { 
    return _lhrs_wrapper<mockturtle::mig_network>( filename );
  } );
}

} // namespace revkit
