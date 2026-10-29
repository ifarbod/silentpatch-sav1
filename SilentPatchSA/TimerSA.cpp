#include "StdAfxSA.h"
#include "TimerSA.h"

ExternalRef<int> CTimer::m_snTimeInMilliseconds(AddressByVersion<int**>(0x4242D1));
ExternalRef<float> CTimer::m_fTimeStep(AddressByVersion<float**>(0x41DE4F + 2));

bool CTimer::HasGameBindings()
{
	return EnsureBindings(m_snTimeInMilliseconds, m_fTimeStep);
}
