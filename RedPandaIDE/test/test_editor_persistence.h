#ifndef TEST_EDITOR_PERSISTENCE_H
#define TEST_EDITOR_PERSISTENCE_H
#include "test_editor_base.h"
class TestEditorPersistence : public TestEditorBase {
    Q_OBJECT
private slots:
    void enabling_backup_creates_one_snapshot_and_disable_cleans_it();
    void failed_backup_commit_keeps_confirmed_snapshot_and_can_retry();
    void failed_backup_move_keeps_previous_snapshot_until_new_commit();
};
#endif
