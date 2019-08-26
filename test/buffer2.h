#pragma once

#include "common.h"
#include "queue.h"


namespace srt
{

    template <class ArgType>
    class EventSlotBase
    {
    public:
        virtual void emit(ETransmissionEvent tev, ArgType var) = 0;
        virtual ~EventSlotBase() {}
    };

    template <class ArgType>
    class SimpleEventSlot : public EventSlotBase<ArgType>
    {
    public:
        typedef void dispatcher_t(void* opaque, ETransmissionEvent tev, ArgType var);
        void* opaque;
        dispatcher_t* dispatcher;

        SimpleEventSlot(void* op, dispatcher_t* disp) : opaque(op), dispatcher(disp) {}

        void emit(ETransmissionEvent tev, ArgType var) ATR_OVERRIDE
        {
            (*dispatcher)(opaque, tev, var);
        }
    };

    template <class Class, class ArgType>
    class ObjectEventSlot : public EventSlotBase<ArgType>
    {
    public:
        typedef void (Class::* method_ptr_t)(ETransmissionEvent tev, ArgType var);

        method_ptr_t pm;
        Class* po;

        ObjectEventSlot(Class* o, method_ptr_t m) : pm(m), po(o) {}

        void emit(ETransmissionEvent tev, ArgType var) ATR_OVERRIDE
        {
            (po->*pm)(tev, var);
        }
    };


    template <class ArgType>
    struct EventSlot
    {
        mutable EventSlotBase<ArgType>* slot;
        // Create empty slot. Calls are ignored.
        EventSlot() : slot(0) {}

        // "Stealing" copy constructor, following the auto_ptr method.
        // This isn't very nice, but no other way to do it in C++03
        // without rvalue-reference and move.
        EventSlot(const EventSlot& victim)
        {
            slot = victim.slot; // Should MOVE.
            victim.slot = 0;
        }

        EventSlot(void* op, typename SimpleEventSlot<ArgType>::dispatcher_t* disp)
        {
            slot = new SimpleEventSlot<ArgType>(op, disp);
        }

        template <class ObjectClass>
        EventSlot(ObjectClass* obj, typename ObjectEventSlot<ObjectClass, ArgType>::method_ptr_t method)
        {
            slot = new ObjectEventSlot<ObjectClass, ArgType>(obj, method);
        }

        void emit(ETransmissionEvent tev, ArgType var)
        {
            if (!slot)
                return;
            slot->emit(tev, var);
        }

        ~EventSlot()
        {
            if (slot)
                delete slot;
        }
    };

}




//class 


std::vector<EventSlot> m_Slots[TEV__SIZE];





class CBuffer
{

public:

    void add();

    void ack();

    void read();

public:

    bool isDataAvailableToRead();

    bool CanWrite();

    /// Query how many buffer space left for data receiving.
    ///
    /// @return size of available buffer space (including user buffer) for data receiving.
    /// 
    size_t getAvailableSize() const;

    

private:

    void OnAvailable();

};




class CSndBuffer2
{

public:

public:

    enum Events
    {
        AVAILABLE_TO_WRITE,
    };

public:



};



