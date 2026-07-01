#include "blackbox_resume.h"

void blackbox_resume_scan_init(blackbox_resume_scan *scan)
{
    scan->highest_session_seq = 0U;
    scan->has_header = false;
    scan->last_valid_slot = 0U;
    scan->has_valid = false;
}

void blackbox_resume_scan_slot(blackbox_resume_scan *scan, uint32_t slot,
                               bool is_valid, bool is_header,
                               uint32_t session_seq)
{
    if (!is_valid) {
        return;
    }

    /* A valid record marks how far the write head reached; the cursor resumes
     * right after the highest such slot so new writes never collide with it. */
    scan->last_valid_slot = slot;
    scan->has_valid = true;

    if (is_header && (!scan->has_header ||
                      session_seq > scan->highest_session_seq)) {
        scan->highest_session_seq = session_seq;
        scan->has_header = true;
    }
}

blackbox_resume_seed blackbox_resume_decide(const blackbox_resume_scan *scan)
{
    blackbox_resume_seed seed;

    /* Fresh region: start ids at 0 (first START yields 1) and the cursor at 0. */
    seed.session_seq = scan->has_header ? scan->highest_session_seq : 0U;
    seed.cursor = scan->has_valid ? scan->last_valid_slot + 1U : 0U;
    return seed;
}
