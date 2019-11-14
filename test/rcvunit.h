struct CRcvUnit
{
   CPacket m_Packet;		// packet
   enum Flag { FREE = 0, GOOD = 1, PASSACK = 2, DROPPED = 3 };
   Flag m_iFlag;			// 0: free, 1: occupied, 2: msg read but not freed (out-of-order), 3: msg dropped
};

class CUnitPool
{

public:

   CUnitPool();
   ~CUnitPool();

public:     // Storage size operations

      /// Initialize the unit queue.
      /// @param [in] size queue size
      /// @param [in] mss maximum segment size
      /// @param [in] version IP version
      /// @return 0: success, -1: failure.

   int init(int size, int mss, int version);

      /// Increase (double) the unit queue size.
      /// @return 0: success, -1: failure.

   int increase();

      /// Decrease (halve) the unit queue size.
      /// @return 0: success, -1: failure.

   int shrink();

public:     // Operations on units

      /// find an available unit for incoming packet.
      /// @return Pointer to the available unit, NULL if not found.

    CRcvUnit* getNextAvailUnit();


   void makeUnitFree(CRcvUnit* unit);

   void makeUnitGood(CRcvUnit* unit);

public:

    inline int getIPversion() const { return m_iIPversion; }


private:
   struct CQEntry
   {
      CUnit* m_pUnit;		// unit queue
      char* m_pBuffer;		// data buffer
      int m_iSize;		// size of each queue

      CQEntry* m_pNext;
   }
   *m_pQEntry,			// pointer to the first unit queue
   *m_pCurrQueue,		// pointer to the current available queue
   *m_pLastQueue;		// pointer to the last unit queue

   CRcvUnit* m_pAvailUnit;         // recent available unit

   int m_iSize;			// total size of the unit queue, in number of packets
   int m_iCount;		// total number of valid packets in the queue

   int m_iMSS;			// unit buffer size
   int m_iIPversion;		// IP version

private:
   CUnitPool(const CUnitPool&);
   CUnitPool& operator=(const CUnitPool&);
};