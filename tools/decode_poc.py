#!/usr/bin/env python3
"""Offline decoder only. Never opens a device, changes state, or calls sysctl."""
import argparse
import json
import pathlib
import struct
import sys

HEADER = struct.Struct('<QIIIIQQQQQQ16s')
EVENT = struct.Struct('<QQQQQQII18Q')
NAMES = ['invalid','Bound','StartResult','Stopped','OslBound','OslDetached',
 'MapResult','TxEnter','TxExit','Descriptor','ReclaimEnter','ReclaimExit',
 'FreeEnter','FreeExit','SuspendEnter','SuspendExit','ResetEnter','ResetExit',
 'FifoBinding','FlushEnter','FlushExit','FatalEnter','FatalExit','PoolBound',
 'PacketBound','SkyPrepareEnter','SkyPrepareExit','SkyCompleteEnter','SkyCompleteExit',
 'SkyCopyEnter','SkyCopyExit','SkyDequeue','SkyEnqueue','Limitation','Acquisition', 'MapperReady','MapperPrepared','MapperSubmitted','MapperCompleted',
 'MapperQuarantine','MapperRejected','MapperHalted','MapperInventory','TxInitEnter','TxInitExit','InvalidRegister','MapperSelection',
 'RxFillEnter','RxFillExit','RxMapOriginal','RxDescriptor','RxReclaimEnter','RxReclaimExit','RxStatusRead','RxObservationLimit',
 'RxMode','RxMapperPrepared','RxMapperMapped','RxMapperCompleted','RxMapperQuarantine','RxMapperRejected','RxMapperHalted','RxMapperInventory',
 'RxResetEnter','RxResetExit','RxResetStatus','RxResetCompleted','RxInitEnter','RxInitExit','RxEnableEnter','RxEnableExit','RxCleanupDenied',
 'TxSoftwareDetached','TxResetStatus','TxResetCompleted','TxCleanupDenied','TxCleanupMode','TxReclaimNotified',
 'TxPacketShape','TxPacketMode']
TX_REJECT_BITS=['halted','no_mapper','no_packet','ring_count','ring_indices','missing_vectors',
 'address_offset','chain_mode_disabled','packet_queue','empty_fragment','length_limit',
 'allocation_limit','no_data','address_overflow','fragment_limit','page_limit','cycle','length_exceeds_backing']
STATUS = {0:'disabled/no enable argument',1:'waiting for target',2:'Darwin/UUID rejected',
          3:'symbol/layout rejected',4:'callback/route failure (pass-through)',
          5:'routes armed; no bound target yet',6:'provider bound',8:'identity table uncertain; tagged-path tracing disabled',
          9:'Lilu patcher ready; waiting for target notification',10:'Lilu registration failed',
          11:'IOService observer allocation/registration failed (pass-through)',
          12:'AppleVTD not observed; correction inactive',13:'device mapper unavailable; correction inactive',
          14:'old -brcmvtdtrace argument present; correction disabled'}
ACQUISITION = {1:'Registered',2:'PatcherReady',3:'Dispatch',4:'TargetEntered',
               5:'BinaryAccepted',6:'LayoutAccepted',7:'RoutesInstalled',8:'ObserverInstalled',
               9:'ProviderRejected',10:'AcquisitionFailed'}
EXTRA_NAMES={91:'NativeTxStatusData',92:'TxStatusWordMode',93:'NativeReadResult',94:'ReadPolicyMode',
             95:'MapperNoCredit',96:'MapperAdmissionResumed',97:'MapperCreditReturned',
             98:'TxQualification',99:'TxQualificationSpan',100:'TxDisposition',101:'TxQuiescence',102:'TxPrivate',103:'RuntimeModeSelected'}
RUNTIME_MODES={1:'NATIVE_PASSTHROUGH',2:'APPLEVTD_CORRECTIVE_EXPERIMENTAL'}
STATUS[15]='native passthrough; no verified system AppleVTD selected'
PRIVATE_STAGES={1:'mode',2:'private_backing_leased',3:'packet_association_detached',
 4:'exact_packet_returned_native',5:'native_free_observed',6:'same_pointer_tx_attempt',
 7:'normal_range2_retired',8:'unpublished_retired',9:'reset_generation_selected',
 10:'reset_retirement_denied',11:'EXPERIMENTAL_300us_drain_begin',12:'EXPERIMENTAL_300us_drain_end',
 13:'EXPERIMENTAL_old_DMA_retired_credit_returned',14:'MD_retirement_failed',
 15:'generation_invalidated',16:'inventory',17:'EXPERIMENTAL_active_association_ended_at_reset_terminal'}
QUAL_REASONS=['cleanup_off','caller_mismatch','range_mismatch','control_flag',
 'ring_count','ring_index','missing_vectors','no_mapping','queue_or_owner',
 'start_index','end_or_span','map_record','native_span_count','pre_packet_slots',
 'returned_candidate_mismatch','ring_identity_changed','post_index',
 'post_packet_slots_not_zero','descriptor_span_not_cleared','observation_incomplete']
QUAL_STAGES={1:'entry',2:'candidate',3:'post',4:'outcome',5:'reset_opportunity',
             6:'sync_context',7:'coverage',8:'lifecycle_attempt',9:'pre_mapping'}
QUAL_FIELDS={
 1:['id','serial','caller','range','control','actual_cleanup_caller','entry_reasons','ring_count','in','out','maps','packets','descriptors','owner','sync_id','sync_mode','wlc','context_state'],
 2:['id','serial','actual_preselected_serial','observed_pre_packet_serial','independent_pre_reasons','native_span','expected_start','expected_end','expected_map_record','expected_packet','expected_owner','expected_queue','expected_span','captured_span','mapping_state_after_native','consumed','prepared','observed_serial_stable'],
 3:['id','serial','independent_post_reasons','returned_packet','ring_count','in','out','maps','packets','descriptors','owner','captured_post_span','actual_candidate_matches'],
 4:['id','serial','note_detached_reached','actual_detached_predicate','certificate_present','notification_done','cleanup_blocked','first_quarantine_reason','tracked_return_withheld','native_context_class','mapping_state','record_ordinal'],
 5:['id','serial','epoch','disabled_acknowledged','mapping_epoch','detached','detached_thread','detached_caller','notified','cleanup_blocked','consumed','prepared','mapping_state','halted','cleanup_enabled','reset_thread','elapsed_ns_since_qualification','first_quarantine_reason'],
 6:['sync_id','bitmap','mode'],7:['count','coverage_reason'],8:['unused','serial','action','mapping_state','poisoned'],
 9:['id','returned_serial','pre_serial','pre_packet','pre_owner','pre_queue','pre_start','pre_end','pre_map_record','pre_state','pre_consumed','pre_prepared','independent_pre_reasons']}
READ_USES={0:'native consumer (not ownership proof)',1:'TX completion status',2:'RX completion status',3:'TX reset status',4:'RX reset status'}

def decode(blob):
    if len(blob)<HEADER.size: raise ValueError('truncated header')
    magic, version, size, capacity, status, nxt, busy, tables, stop, provider, controller, uuid = HEADER.unpack_from(blob)
    if magic!=0x3154434152545642 or version not in (3,4,5,6,7,10,11,12,13) or size!=EVENT.size or capacity!=32768:
        raise ValueError('incompatible format')
    rx_entries=1025 if version>=4 else 0
    tx_entries=65 if version>=6 else 64
    first_entries=1 if version in (10,11,12,13) else 0
    if len(blob)!=HEADER.size+(capacity+tx_entries+rx_entries+first_entries)*size: raise ValueError('truncated/oversized capture')
    header=dict(version=version,event_size=size,capacity=capacity,gate_status=status,
                gate=STATUS.get(status,'unknown'),next_sequence=nxt,busy_drops=busy,
                table_drops=tables,stop_after=stop,provider=hex(provider),controller=hex(controller),
                target_uuid=uuid.hex(),snapshot_atomic=False)
    events=[]
    for i in range(capacity):
        seq,ns,thread,obj,pkt,aux,typ,flags,*data=EVENT.unpack_from(blob,HEADER.size+i*size)
        if not seq or seq>nxt or seq<=max(0,nxt-capacity): continue
        if (seq-1)%capacity!=i: raise ValueError('slot/sequence mismatch')
        events.append(dict(sequence=seq,time_ns=ns,thread=hex(thread),event=NAMES[typ] if typ<len(NAMES) else EXTRA_NAMES.get(typ,typ),
                           object=hex(obj),packet=hex(pkt),auxiliary=hex(aux),flags=flags,payload=data))
        if typ==34:
            events[-1]['acquisition_stage']=ACQUISITION.get(flags,'unknown')
        if typ==103:
            events[-1]['runtime_mode']=RUNTIME_MODES.get(flags,'UNKNOWN')
        if version>=13 and typ==102:
            p=dict(stage=PRIVATE_STAGES.get(flags,'unknown'),serial=data[0],generation=data[1],
                backing=hex(data[2]),pending=bool(data[3]),active_packet=hex(data[4]),segments=data[5],
                first_iova=hex(data[6]),owner=hex(data[7]),register_identity=hex(data[8]),
                start=data[9],end=data[10],value=data[11],experimental_retired_total=data[12],
                hardware_contract='EXPERIMENTAL; NOT REV49 SOURCE-PROVEN; NOT RELEASE AUTHORITY',
                pointer_reuse_is_not_proof_of_requeue=True)
            if flags==9:
                p=dict(stage=PRIVATE_STAGES[flags],serial_fence=data[0],old_generation=data[1],
                    new_generation=data[2],ticket=data[3],reset_thread=hex(data[4]),regs=hex(data[5]),
                    ring_count=data[6],base=hex(data[7]),mask=hex(data[8]))
                if data[9] or data[10]:
                    p.update(native_reset_caller=hex(data[9]),observed_core_revision=data[10],
                             caller_is_ownership_authority=False)
            if flags in (10,11,12):p['ticket']=data[11]
            events[-1]['private_tx']=p
        if version>=13 and typ==98:
            q=dict(zip(QUAL_FIELDS.get(flags,[]),data))
            q.update(stage=QUAL_STAGES.get(flags,'unknown'),ownership_permission=False)
            for key in ('entry_reasons','independent_pre_reasons','independent_post_reasons'):
                if key in q:
                    names=[s for i,s in enumerate(QUAL_REASONS) if q[key]&(1<<i)]
                    q[key+'_names']=names
                    q[key+'_first']=names[0] if names else None
            if flags==2:
                q['warning']='Independent reconstruction against returned mapping; NOT proof that production preselection ran. Source caller gate may have skipped it.'
            if flags==4:
                q['native_context']={0:'unknown',1:'free-owning native caller',2:'native sync mode-1 disposal branch',3:'native sync requeue-capable branch'}.get(data[9],'unknown')
                q['warning']='Context is NOT an executed free/requeue or disposal certificate. '+('Tracked return was withheld.' if q['tracked_return_withheld'] else 'Tracked return was not withheld; native disposition is not inferred.')
            if flags==6:
                q['warning']='Original synchronization arguments only; mode != 1 is requeue-capable, not proof that requeue occurs.'
            if flags==5:
                checks=[('cleanup_disabled',not data[14]),('genuine_halt',bool(data[13])),
                        ('disabled_ack_absent',not data[3]),('epoch_absent',not data[2]),
                        ('not_detached',not data[5]),('different_detach_thread',data[6]!=data[15]),
                        ('different_epoch',data[4]!=data[2]),('cleanup_blocked',bool(data[9])),
                        ('not_notified',not data[8]),('not_consumed',not data[10]),
                        ('not_prepared',not data[11]),('not_quarantine',data[12]!=6)]
                q['missing_requirements']=[name for name,failed in checks if failed]
                q['warning']='Read-only pre-cleanup opportunity snapshot; actual CAS/MD result is only in unchanged cleanup events.'
            if flags==7:
                q['coverage']={1:'context slots full',2:'tracked qualification budget reached',3:'scratch slots busy',4:'record budget reached',5:'watch busy'}.get(data[1],'unknown')
            if flags==8:
                q['action_name']={1:'wrapped TX attempt',2:'wrapped Free attempt'}.get(data[2],'unknown')
                q['warning']='Attempt observed before existing retention decision; NOT proof original free/TX executed.'
            events[-1]['tx_qualification']=q
        if version>=13 and typ==100:
            q=dict(zip(['id','serial','mode','coverage','tag_flags1','tag_flags7','bss_index_raw',
                        'cached_scb','primary_special_scb','alternate_special_scb','recovery_class',
                        'bss_slot','queue','priority','precedence','count_limit','hint','queue_precisions'],data))
            q['bss_index']=data[6] if data[6]<(1<<63) else data[6]-(1<<64)
            q['queue_count']=data[15]&0xffffffff;q['queue_limit']=data[15]>>32
            q['coverage_names']=[name for i,name in enumerate(['tag_valid','main_band_valid','other_band_checked',
                'bss_slot_observed','queue_observed','priority_valid','sampled_anchors_stable']) if data[3]&(1<<i)]
            q['recovery']={0:'unknown',1:'null SCB at sampled recovery predicates',2:'primary special SCB early return',
                3:'alternate special SCB early return',4:'mutating native SCB recovery/lookup still required'}.get(data[10],'unknown')
            q['branch_hint']={0:'UNKNOWN',1:'mode-1 disposal path',2:'null-recovery disposal path',3:'tag-bit disposal path',
                4:'mode-2 primary-special disposal path',5:'mode-2 destination/SCB/A-MPDU filters still required',
                6:'native SCB recovery still required',7:'queue/priority observation unavailable',
                8:'sampled precedence full: disposal path',9:'sampled precedence has room: requeue eligible, NOT executed'}.get(data[16],'unknown')
            q.update(ownership_permission=False,native_disposition_executed=False,
                warning='Passive pre-disposition predicate snapshot only. Native recovery/tag mutation, intervening packets, callbacks and queue changes are NOT simulated. Whether the return was withheld is reported by the qualification outcome, not this snapshot. No disposal/unmap/requeue authority.')
            events[-1]['tx_disposition']=q
        if version>=13 and typ==101:
            stages={1:'flush_begin',2:'coalesced_native_read',3:'flush_returned',4:'sync_entered',
                    5:'tracked_reclaim',6:'control_activity',7:'sync_left',8:'coverage',9:'native_fatal'}
            fields=['transaction','wlc','bitmap','core_revision','mode','phase','register_base','ordinal',
                    'sample_keys','sample_overflow','control_events','last_control_ordinal','native_fatals',
                    'cross_thread_activity','serial','caller','range','diagnostic_busy_drops']
            if flags==2:
                fields=['transaction','region','caller','width','first_value','last_value','read_count',
                        'value_changes','seen_value_classes','first_ordinal','last_ordinal','first_time_ns',
                        'last_time_ns','owner','register_base','sample_overflow','phase','sample_index']
            elif flags==6:fields=['transaction','operation','returned','phase','ordinal','transaction_thread','cross_thread_activity']
            elif flags==8:fields=['transaction','coverage_reason','diagnostic_busy_drops','diagnostic_record_drops','transactions']
            q=dict(zip(fields,data));q.update(stage=stages.get(flags,'unknown'),ownership_permission=False,
                native_disposition_executed=False,warning='Diagnostic native-read observations, NOT a hardware-quiescence/release certificate. Coalesced rows retain first/last values and counts, NOT every intermediate value/order. Missing, concurrent or overflowed evidence is UNKNOWN. Flush return alone is not success.')
            if flags==2:
                q['region_name']={1:'D11 channel status',2:'DMA TX control',3:'DMA TX status0',
                    4:'object address readback',5:'SHM offset 0x7e halfword candidate',6:'MAC control',7:'MAC interrupt status'}.get(data[1],'unknown')
                q['seen_names']=[n for i,n in enumerate(['zero','all_ones_at_width','status0_high_nibble_2','other']) if data[8]&(1<<i)]
            if flags==8:q['coverage_name']={1:'observer busy',2:'contexts full',3:'sample keys full',4:'transaction budget',5:'record budget',6:'unmatched sync',7:'unmatched tracked reclaim',8:'old unmatched flush replaced'}.get(data[1],'unknown')
            events[-1]['tx_quiescence']=q
        if version>=13 and typ==99:
            events[-1]['tx_qualification_span']=dict(zip(
                ['id','serial','ordinal','pre_packet','post_packet','pre_address_low','pre_address_high',
                 'post_address_low','post_address_high','index','post_observed'],data))
            events[-1]['tx_qualification_span']['ownership_permission']=False
        if version>=7 and typ==40:
            events[-1]['tx_admission']=dict(rejected_bits=hex(data[0]),
                reasons=[name for bit,name in enumerate(TX_REJECT_BITS) if data[0]&(1<<bit)],
                chain_mode=bool(data[1]),head_length=data[2],head_maximum=data[3],
                head_next=hex(data[4]),head_next_packet=hex(data[5]),ring_count=data[6],
                consumer=data[7],producer=data[8],address_offset=data[9],
                fragments=data[10],total_bytes=data[11],pages=data[12],
                mapping_serial=data[13],segments=data[14])
        if version>=7 and typ==78:
            events[-1]['tx_packet_shape']=dict(chain_mode=bool(flags),serial=data[0],
                fragments=data[1],head_length=data[2],total_bytes=data[3],
                segments=data[4],fragment_lengths=data[5:5+min(data[1],8)])
        if version>=7 and typ==79:
            events[-1]['tx_chain_mode']=bool(flags)
        if version>=11 and typ==91:
            events[-1]['native_tx_status_data']=dict(returned=hex(data[0]),caller=hex(data[1]),
                changed_native_result=False,ownership_permission=False,
                meaning='Exact native uint32 data word returned; no plugin global quarantine/halt from this value alone.')
        if version>=11 and typ==92:
            events[-1]['native_tx_status_mode']=dict(enabled=bool(flags),validated_return_pc=hex(data[0]))
        if version>=12 and typ in (45,93):
            events[-1]['read_policy']=dict(value=hex(data[0]),caller=hex(data[1]),
                use=READ_USES.get(data[2],'unknown'),native_result_changed=False,
                ownership_permission=False,scoped_policy=(True if typ==93 else bool(data[3])),
                decision=('native consumer decides; no added global halt' if typ==93 else 'mapping admission halted; ownership unproven'))
        if version>=12 and typ==94:
            events[-1]['scoped_read_policy']=dict(enabled=bool(flags),ownership_read_sites=data[0])
        if version>=13 and typ in (95,96,97):
            events[-1]['tx_pressure']=dict(counter=data[0],permanent_failure=False,
                meaning={95:'No Empty entry reserved; this packet is refused. Ring samples power-of-two ordinals; footer counts every no-credit scan.',
                         96:'First mapper-backed submission recorded after pressure was observed.',
                         97:'First qualified normal mapping release recorded after pressure was observed.'}[typ])
        if typ in (35,46):
            events[-1]['mapper_source']={0:'unavailable (or pre-0.2.1 MapperReady)',
                1:'device-specific',2:'verified system AppleVTD'}.get(flags,'unknown')
        if typ==46:
            events[-1]['mapper_selection']=dict(specific=hex(data[0]),system=hex(data[1]),
                observed_applevtd=hex(data[2]),iommu_parent_present=bool(data[3]),
                observed_active=bool(data[4]))
        if typ==64:
            events[-1]['reset_acknowledgement']=dict(returned=bool(flags),epoch=data[14],
                status=hex(data[15]),status_read_observed=bool(data[16]),certified_mappings=data[13])
        if typ==66:
            events[-1]['reset_cleanup']=dict(serial=data[0],epoch=data[1],index=data[2],
                iova=hex(data[3]),bytes=data[4],reset_thread=hex(data[5]),normal_receive=False)
        if typ==74:
            events[-1]['tx_reset_cleanup']=dict(serial=data[0],epoch=data[1],start=data[2],end=data[3],
                detached_caller=hex(data[4]),reset_thread=hex(data[5]),normal_transmission=False)
        if typ in (1,4) and flags&1:
            events[-1]['coverage']='Late observation; prior start/attach and packet lifetime are UNKNOWN'
    events.sort(key=lambda x:x['sequence'])
    header['retained_records']=len(events)
    header['missing_in_retained_window']=min(nxt,capacity)-len(events)
    header['interpretation']='Gaps/overwrites/truncation are unknown evidence, not proof of missing completion. Direct msgbuf address != proof of Skywalk provenance.'
    snapshot=[]
    for i in range(64):
        seq,ns,thread,obj,pkt,aux,typ,flags,*data=EVENT.unpack_from(blob,HEADER.size+(capacity+i)*size)
        if pkt:
            snapshot.append(dict(slot=i,unstable=bool(flags&0x80000000),packet=hex(pkt),queue=hex(obj),
                                 descriptor=hex(aux),serial=data[0],state=data[1],owner=hex(data[2]),
                                 segments=data[3],bytes=data[4],start=data[5],end=data[6],map_record=hex(data[7]),
                                 poisoned=bool(data[8]),consumed=bool(data[9]),halted=bool(data[10])))
            if version>=6:
                snapshot[-1].update(first_reason=data[11],software_detached=bool(data[12]),
                    detached_caller=hex(data[13]),detached_thread=hex(data[14]),
                    cleanup_blocked=bool(data[15]),reset_epoch=data[16],reset_completed_total=data[17])
            if version>=13 and flags&0x40000000:
                for key in ['software_detached','detached_caller','detached_thread','reset_epoch','reset_completed_total']:
                    snapshot[-1].pop(key,None)
                snapshot[-1].update(private_backing=True,pending_terminal_generation=bool(data[12]),
                    generation=data[13],private_backing_address=hex(data[14]),active_packet_association=hex(data[16]),
                    experimental_retired_total=data[17],packet_is_opaque_historical_identity=True)
    if version>=6:
        data=EVENT.unpack_from(blob,HEADER.size+(capacity+64)*size)[8:]
        header['tx_totals']=dict(cleanup_enabled=bool(data[0]),halt_reason=data[1],
            used=data[2],quarantined=data[3],capacity=data[4],reset_completed=data[5],
            reset_epochs=data[6],quarantine_events=data[7],first_halt_time_ns=data[8],
            first_halt_sequence=data[9],software_detached_events=data[10],snapshot_atomic=False)
        if version>=13:
            auto_mode=(data[17]>>60)&3
            if auto_mode:
                header['runtime_mode']=RUNTIME_MODES.get(auto_mode,'UNKNOWN')
            if data[17]&(1<<63):
                header['tx_totals']['private_tx_experimental_gate']=True
                header['tx_totals']['private_pending_old']=data[17]&((1<<60)-1)
            header['tx_totals']['pressure']=dict(no_credit_events=data[11],
                reservations_after_pressure=data[12],normal_completions_after_pressure=data[13],
                submissions_after_pressure=data[14],first_no_credit_time_ns=data[15],
                first_no_credit_sequence=data[16],snapshot_atomic=False,
                interpretation='Cumulative after observing first pressure, not per-episode/atomic occupancy. No-credit does not halt/freeze; genuine halts still do. Progress does not release or certify quarantine.')
    header['mapping_snapshot']=snapshot
    header['mapping_slots_used']=len(snapshot)
    header['quarantined_entries']=sum(e['state']==6 or e['poisoned'] for e in snapshot)
    header['mapping_snapshot_atomic']=False
    if rx_entries:
        rx=[]
        for i in range(rx_entries):
            seq,ns,thread,obj,pkt,aux,typ,flags,*data=EVENT.unpack_from(blob,HEADER.size+(capacity+tx_entries+i)*size)
            if i==1024:
                header['rx_totals']=dict(enabled=bool(data[0]),halted=bool(data[1]),capacity=data[2],
                    prepared=data[3],mapped=data[4],completed=data[5],quarantine_events=data[6],snapshot_atomic=False)
                if version>=5:
                    header['rx_totals'].update(reset_completed=data[7],reset_epochs=data[8])
            elif pkt:
                rx.append(dict(slot=i,unstable=bool(flags&0x80000000),queue=hex(obj),packet=hex(pkt),
                    descriptor=hex(aux),serial=data[0],state=data[1],owner=hex(data[2]),virtual_address=hex(data[3]),
                    bytes=data[4],iova=hex(data[5]),map_record=hex(data[6]),start=data[7],poisoned=bool(data[8]),
                    consumed=bool(data[9]),halted=bool(data[10])))
                if version>=5:
                    rx[-1].update(reset_epoch=data[11],reset_thread=hex(data[12]),reset_acknowledged=bool(data[13]))
        header['rx_mapping_snapshot']=rx
        header['rx_mapping_slots_used']=len(rx)
        header['rx_quarantined_entries']=sum(e['state']==6 or e['poisoned'] for e in rx)
    if first_entries:
        seq,ns,thread,obj,pkt,aux,typ,flags,*data=EVENT.unpack_from(blob,HEADER.size+(capacity+tx_entries+rx_entries)*size)
        if typ!=90: raise ValueError('invalid first-failure footer')
        if data[4]>13: raise ValueError('invalid first-failure frame count')
        pcs=data[5:5+data[4]]
        header['first_failure']=dict(captured=data[0] in (41,61),unstable=bool(flags&0x80000000),
            disabled=bool(flags&0x40000000),trigger_type=NAMES[data[0]] if data[0]<len(NAMES) else data[0],
            trigger_sequence=seq,trigger_time_ns=ns,thread=hex(thread),reason=flags&0x3fffffff,
            target_base=hex(data[1]),target_size=data[2],stop_after_at_trigger=data[3],
            frame_count=data[4],return_pcs=[hex(pc) for pc in pcs],
            target_return_offsets=[hex(pc-data[1]) if data[1]<=pc<data[1]+data[2] else None for pc in pcs],
            caveat='Stack is from the first permanent mapper halt, NOT a native fault verdict. No pointer is dereferenced offline.')
        header['interpretation']+=' Format 10 retains the first permanent TX/RX halt and a bounded recovery tail by default; live mapping totals are later non-atomic observations. Old reason-12 trace freeze is unchanged.'
    header['interpretation']+=' Quarantine alone is not proof of hardware quiescence. MapperCompleted/RxMapperCompleted document normal cleanup; format-5 RxResetCompleted separately documents acknowledged-reset plus software-detachment cleanup, NOT received data.'
    return header,events

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',help='binary capture or - for stdin')
    ap.add_argument('--save',type=pathlib.Path,help='save stdin evidence once, refuses overwrite')
    args=ap.parse_args()
    blob=sys.stdin.buffer.read() if args.input=='-' else pathlib.Path(args.input).read_bytes()
    header,events=decode(blob)
    if args.save:
        import os
        fd=os.open(args.save,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        with os.fdopen(fd,'wb') as f: f.write(blob)
    print(json.dumps({'header':header}))
    for event in events: print(json.dumps(event))

if __name__=='__main__': main()
