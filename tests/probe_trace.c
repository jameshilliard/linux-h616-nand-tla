/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included after lifecycle.c, whose ordinary assertions still run first. */
static unsigned traced_probe(FILE *out,unsigned children,unsigned mask,unsigned variant,unsigned fault)
{
    struct device_node nodes[4]={
        {.count=1,.cs={0},.rb={0}}, {.count=1,.cs={1},.rb={1}},
        {.count=1,.cs={2},.rb={2}}, {0}};
    struct platform_device p={.dev.of_node=&nodes[3]};
    defaults(); fail_at=fault; expected_scans=0;
    if (children) nodes[3].child=nodes;
    for (unsigned i=0;i<children;i++) {
        if (i+1<children) nodes[i].next=&nodes[i+1];
        if (!(mask&BIT(i))) nodes[i].status="disabled";
        if (!FILTERED_CHILDREN || of_device_is_available(&nodes[i])) expected_scans++;
    }
    if (children && variant==1) nodes[children-1].absent=true;
    if (children>1 && variant==2) nodes[children-1].cs[0]=0;
    if (children && variant==3) nodes[children-1].count=0;
    probe_trace_file=out; probe_trace_nodes=nodes; probe_trace_count=children;
    probe_trace_events=0; probe_trace_nfc=NULL;
    memset(probe_trace_chips,0,sizeof(probe_trace_chips));
    fprintf(out,"{\"children\":%u,\"mask\":%u,\"variant\":%u,\"fault\":%u,\"available\":[",
            children,mask,variant,fault);
    unsigned a=0;
    for (unsigned i=0;i<children;i++) if (of_device_is_available(&nodes[i]))
        fprintf(out,"%s%u",a++?",":"",i+1);
    fprintf(out,"],\"events\":[");
    int ret=sunxi_nfc_probe(&p);
    if (!ret) {
        probe_trace_nfc=p.data;
        CHECK(scanned==expected_scans && registered==expected_scans);
        probe_trace_event("probe-ok",NULL);
        sunxi_nfc_remove(&p);
    } else {
        CHECK(ret<0 && !registered);
        probe_trace_event("probe-error",NULL);
    }
    probe_trace_event("finish",NULL);
    fprintf(out,"],\"returncode\":%d,\"faultable_events\":%u}\n",ret,events);
    probe_trace_file=NULL;
    unsigned last=events;
    release_devres(); cases_run++;
    return last;
}

int main(void)
{
    CHECK(!lifecycle_main());
    FILE *out=fopen("probe-traces.jsonl","w"); CHECK(out);
    unsigned before=cases_run;
    for (unsigned n=0;n<=3;n++) for (unsigned mask=0;mask<BIT(n);mask++)
        for (unsigned v=0;v<(n?4U:1U);v++) {
            unsigned count=traced_probe(out,n,mask,v,0);
            for (unsigned f=1;f<=count;f++) traced_probe(out,n,mask,v,f);
        }
    CHECK(!fclose(out));
    printf("Probe trace cases: %u; assertions including lifecycle: %u\n",cases_run-before,assertions);
    puts("ALL PROBE TRACE CHECKS PASSED");
    return 0;
}
