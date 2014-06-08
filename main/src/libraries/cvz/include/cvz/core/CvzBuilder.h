#ifndef __CVZ_BUILDER_H__
#define __CVZ_BUILDER_H__

#include "ICvz.h"
#include "CvzMMCM.h"
#include "CvzMLP.h"

namespace cvz {
	namespace core {

const std::string TYPE_ICVZ("icvz");
const std::string TYPE_MLP("mlp");
const std::string TYPE_MMCM("mmcm");

class CvzBuilder
{

public:
	static std::string helpMessage()
	{
		std::string s = "CvzBuilder knows the following types : \n";
		s += TYPE_ICVZ;
		s +=" (Convergence Zone Interface)" + '\n';
		s += TYPE_MMCM;
		s += " (Multi Modal Convergence Map)" + '\n';
		s += TYPE_MLP;
		s += " (Multiple Layered Perceptron)" + '\n';
		return s;
	}

	static bool allocate(IConvergenceZone** ptr, const std::string &type)
	{
		if (type == TYPE_ICVZ)
			(*ptr) = new IConvergenceZone();
		else if (type == TYPE_MMCM)
			(*ptr) = new CvzMMCM();
		else if (type == TYPE_MLP)
			(*ptr) = new CvzMLP();
		else
			return false;
		return true;
	}
};

struct ThreadedCvz :yarp::os::RateThread
{
public:

	IConvergenceZone* cvz;

	ThreadedCvz(yarp::os::Property prop, int period) :yarp::os::RateThread(period)
	{
		std::string cvzType = prop.check("type", yarp::os::Value(cvz::core::TYPE_ICVZ)).asString();

		if (cvz::core::CvzBuilder::allocate(&cvz, cvzType))
		{
			cvz->configure(prop);
		}
		else
		{
			std::cout << "This cvz type (" << cvzType << ") is not handled by the builder." << std::endl
				<< cvz::core::CvzBuilder::helpMessage() << std::endl;
			cvz = NULL;
		}
	}

	bool threadInit()
	{
		return (cvz != NULL);
	}

	void run()
	{
		cvz->cycle();
	}

	void threadRelease()
	{
		cvz->interruptModule();
		cvz->close();
		delete cvz;
	}

};
}
}
#endif