#pragma once

#include <cstdint>

class CAnimBlendAssociation;
class CAnimBlendSequence;
class CAnimBlendHierarchy;

struct ListLink
{
	ListLink* pNext;
	ListLink* pPrev;
}; typedef ListLink ListLink;

class CAnimBlendNode
{
private:
	float m_fTheta;
	float m_fOOSinTheta;

	int16_t m_iCurrentFrame;
	int16_t m_iPreviousFrame;

	float m_fTimeRemaining;

	CAnimBlendSequence* m_pSequence;
	CAnimBlendAssociation* m_pAssociation;

public:
	void SetSequence(CAnimBlendSequence* sequence) { m_pSequence = sequence; }
	CAnimBlendSequence* GetSequence() const { return m_pSequence; }
};
static_assert(sizeof(CAnimBlendNode) == 0x18, "Wrong size: CAnimBlendNode");

enum
{
	ABA_FLAG_ISPLAYING = 1,
	ABA_FLAG_ISLOOPED = 2,
	ABA_FLAG_ISBLENDAUTOREMOVE = 4,
	ABA_FLAG_ISFINISHAUTOREMOVE = 8,
	ABA_FLAG_ISPARTIAL = 16,
	ABA_FLAG_ISSYNCRONISED = 32,
	ABA_FLAG_CANEXTRACTVELOCITY = 64,
	ABA_FLAG_CANEXTRACTXVELOCITY = 128,
	ABA_FLAG_USER1 = 256,
	ABA_FLAG_USER2 = 512,
	ABA_FLAG_USER3 = 1024,
	ABA_FLAG_USER4 = 2048,
	ABA_FLAG_USER5 = 4096,
	ABA_FLAG_IGNORE_ROOT_TRANSLATION = 8192,
	ABA_FLAG_REFERENCE_BLOCK = 16384,
	ABA_FLAG_FACIAL = 32768
};

enum AnimBlendCallbackType
{
	ABCB_NONE = 0,
	ABCB_FINISH,
	ABCB_DELETE
};
using CAnimBlendAssocationCallback = void (*)(CAnimBlendAssociation*, void*);

class CAnimBlendAssociation
{
private:
	ListLink m_list;
	int16_t m_iNumAnimBlendNodes;
	int16_t m_animGrp;
	CAnimBlendNode* m_pAnimBlendNodes;

	void* m_pAnimBlendHierarchy;

	float m_fBlendAmount;
	float m_fBlendDelta;

	float m_fCurrentTime;
	float m_fSpeed;
	float m_fTimeStep;

	int16_t m_animId;
	uint16_t m_bitsFlag;

	AnimBlendCallbackType m_cbType;
	CAnimBlendAssocationCallback m_cb;
	void* m_pCBData;

public:
	virtual ~CAnimBlendAssociation() = default;

	void ClearFlag(uint32_t flag) { m_bitsFlag &= ~flag; }

	CAnimBlendNode* GetNode(int32_t iNodeNum) const { return &m_pAnimBlendNodes[iNodeNum]; }
	int32_t GetNumNodes() const { return m_iNumAnimBlendNodes; }

	auto GetNodes() const
	{
		struct NodeRange
		{
			NodeRange(CAnimBlendNode* begin, CAnimBlendNode* end)
				: m_begin(begin), m_end(end)
			{
			}

			auto begin() const { return m_begin; }
			auto end() const { return m_end; }

		private:
			CAnimBlendNode* m_begin;
			CAnimBlendNode* m_end;
		};
		return NodeRange(m_pAnimBlendNodes, m_pAnimBlendNodes + m_iNumAnimBlendNodes);
	}
};
static_assert(sizeof(CAnimBlendAssociation) == 0x3C, "Wrong size: CAnimBlendAssociation");

enum
{
	ABS_FLAG_HASROTATION = 1,
	ABS_FLAG_HASTRANSLATION = 2,
	ABS_FLAG_ISCOMPRESSED = 4,
	ABS_FLAG_DOESNT_OWN_KEYMEM = 8,
	ABS_FLAG_USE_BONE_TAG = 16
};

enum
{
	BONETAG_ROOT = 0,
	BONETAG_PELVIS,
	BONETAG_SPINE,
	BONETAG_SPINE1,
	BONETAG_NECK,
	BONETAG_HEAD,
	BONETAG_L_BROW,
	BONETAG_R_BROW,
	BONETAG_JAW,
	BONETAG_R_CLAVICLE = 21,
	BONETAG_R_UPPERARM,
	BONETAG_R_FOREARM,
	BONETAG_R_HAND,
	BONETAG_R_FINGERS,
	BONETAG_R_FINGER01,
	BONETAG_L_CLAVICLE = 31,
	BONETAG_L_UPPERARM,
	BONETAG_L_FOREARM,
	BONETAG_L_HAND,
	BONETAG_L_FINGERS,
	BONETAG_L_FINGER01,
	BONETAG_L_THIGH = 41,
	BONETAG_L_CALF,
	BONETAG_L_FOOT,
	BONETAG_L_TOE,
	BONETAG_R_THIGH = 51,
	BONETAG_R_CALF,
	BONETAG_R_FOOT,
	BONETAG_R_TOE,
	BONETAG_BELLY = 201,
	BONETAG_R_BREAST = 301,
	BONETAG_L_BREAST
};

class CAnimBlendSequence
{
protected:
	union
	{
		int16_t m_boneTag;
		uint32_t m_nameHashKey;
	};

	uint16_t m_bitsFlag;

	int16_t m_iNumKeyFrames;
	uint8_t* m_pKeyFrames;


public:
	bool HasBoneTag() const { return (m_bitsFlag & ABS_FLAG_USE_BONE_TAG) != 0; }
	int32_t GetBoneTag() const { return m_boneTag; }
};
static_assert(sizeof(CAnimBlendSequence) == 0xC, "Wrong size: CAnimBlendSequence");