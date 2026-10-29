#ifndef __LINKLIST__
#define __LINKLIST__

template <class T>
class CLinkSA
{
	template <class T>
	friend class CLinkListSA;

public:
	inline void Insert(CLinkSA<T>* pAttach) {
		pAttach->m_pNext = m_pNext;
		m_pNext->m_pPrev = pAttach;

		pAttach->m_pPrev = this;
		m_pNext = pAttach;
	}

	inline void InsertBefore(CLinkSA<T>* pAttach) {
		pAttach->m_pPrev = m_pPrev;
		m_pPrev->m_pNext = pAttach;

		pAttach->m_pNext = this;
		m_pPrev = pAttach;
	}

	inline void Remove(void) {
		m_pNext->m_pPrev = m_pPrev;
		m_pPrev->m_pNext = m_pNext;
	}

	inline T& operator*(void) {
		return m_pItem;
	}

	inline const T& operator*(void) const {
		return m_pItem;
	}

private:
	T m_pItem; // 0-4
	// an item
	CLinkSA<T>* m_pPrev; // 4-8
	//next link in the list
	CLinkSA<T>* m_pNext; // 8-12
	//prev link in the list
};

template <class T>
class CLinkListSA {
public:
	CLinkListSA(void)
		: m_plnLinks(nullptr)
	{
	}

	void Init(size_t nNumLinks) {
		m_plnLinks = new CLinkSA<T>[nNumLinks];

		m_lnListHead.m_pNext = &m_lnListTail;
		m_lnListTail.m_pPrev = &m_lnListHead;

		m_lnFreeListHead.m_pNext = &m_lnFreeListTail;
		m_lnFreeListTail.m_pPrev = &m_lnFreeListHead;

		for(size_t i = nNumLinks; i > 0; --i) {
			m_lnFreeListHead.Insert(&m_plnLinks[i - 1]);
		}
	}

	void Shutdown(void) {
		delete[] m_plnLinks;

		m_plnLinks = nullptr;
	}

	CLinkSA<T>* InsertSorted(const T& pItem) {
		CLinkSA<T>* pLink = m_lnFreeListHead.m_pNext;

		if(pLink == &m_lnFreeListTail) {
			return nullptr;
		}

		pLink->m_pItem = pItem;

		pLink->Remove();

		CLinkSA<T>* pInsertAfter = &m_lnListHead;

		while(pInsertAfter->m_pNext != &m_lnListTail && pInsertAfter->m_pNext->m_pItem < pItem) {
			pInsertAfter = pInsertAfter->m_pNext;
		}

		pInsertAfter->Insert(pLink);

		return pLink;
	}

	CLinkSA<T>* Insert(const T& pItem) {
		CLinkSA<T>* pLink = m_lnFreeListHead.m_pNext;

		if(pLink == &m_lnFreeListTail) {
			return nullptr;
		}

		pLink->m_pItem = pItem;

		pLink->Remove();
		m_lnListHead.Insert(pLink);

		return pLink;
	}

	CLinkSA<T>* InsertFront(const T& pItem) {
		return Insert(pItem);
	}

	CLinkSA<T>* InsertBack(const T& pItem) {
		CLinkSA<T>* pLink = m_lnFreeListHead.m_pNext;

		if(pLink == &m_lnFreeListTail) {
			return nullptr;
		}

		pLink->m_pItem = pItem;

		pLink->Remove();
		m_lnListTail.InsertBefore(pLink);

		return pLink;
	}

	void Clear(void) {
		while(m_lnListHead.m_pNext != &m_lnListTail) {
			Remove( m_lnListHead.m_pNext );
		}
	}

	void Remove(CLinkSA<T>* pLink) {
		pLink->Remove();
		m_lnFreeListHead.Insert(pLink);
	}

	CLinkSA<T>* Next(CLinkSA<T>* pCurrent) {
		if(pCurrent == nullptr) {
			pCurrent = &m_lnListHead;
		}

		if(pCurrent->m_pNext == &m_lnListTail) {
			return nullptr;
		}
		return pCurrent->m_pNext;
	}

	CLinkSA<T>* Prev(CLinkSA<T>* pCurrent) {
		if(pCurrent == nullptr) {
			pCurrent = &m_lnListTail;
		}

		if(pCurrent->m_pPrev == &m_lnListHead) {
			return nullptr;
		}
		return pCurrent->m_pPrev;
	}

private:
	CLinkSA<T> m_lnListHead; // 0-12
	//head of the list of active links
	CLinkSA<T> m_lnListTail; // 12-24
	//tail of the list of active links
	CLinkSA<T> m_lnFreeListHead; // 24-36
	//head of the list of free links
	CLinkSA<T> m_lnFreeListTail; // 36-48
	//tail of the list of free links
	CLinkSA<T>* m_plnLinks; // 48-52
	//pointer to actual array of links
};

#endif