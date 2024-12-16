

#pragma once

#include "LogWindowMessages.hpp"

namespace HexEngine
{
    // win_msg.h file ----------
#define SHOW_USED_MESSAGES 0

    const wchar_t* GetMessageText(unsigned int msg);

#ifdef SHOW_USED_MESSAGES
    void ShowUsedMessages(void);
#endif
}
