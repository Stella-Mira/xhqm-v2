#ifndef XHQM_FANTASY
#define XHQM_FANTASY

#include "../algebra/value.h"
#include "../algebra/geometry/linear.h"
#include "../algebra/geometry/geometry.h"
#include "../software/pattern/instance.h"
#include "../metaclass/block.h"
#include "../metaclass/buffer.h"
#include "../metaclass/template.h"
#include "../metaclass/resource.h"
#include "../lattice/tree.hpp"
#include "../network/uuid.h"

namespace xhqm
{
	namespace fantasy
	{
		using id = unsigned int;
		using sint = signed int;
		using uint = unsigned int;
		using rgba = unsigned int;
		using real = float;

		using value = xhqm::real;
		using index = xhqm::index;
		using count = xhqm::count;
		using values = std::vector<value>;
		//xhqm::real == xhqm::fantasy::real == xhqm::fantasy::value 
		//xhqm::rgba == xhqm::fantasy::rgba == xhqm::fantasy::color 

		using matrix = xhqm::matx<value>;
		using vector = xhqm::vect<value>;
		using point = xhqm::poit<value>;
		using topol = xhqm::topol<>;
		using shape = xhqm::shape<>;

		template<typename _type, typename _id>
		using resource = xhqm::resource<_type, _id>;
	}
}

#endif