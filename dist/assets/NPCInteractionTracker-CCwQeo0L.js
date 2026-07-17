const t={interactions:[],listeners:[]},r=(n,s)=>{const e={npcId:n,timestamp:Date.now(),questId:s};t.interactions.push(e),t.listeners.forEach(i=>{i(e)})},c=n=>(t.listeners.push(n),()=>{const s=t.listeners.indexOf(n);s!==-1&&t.listeners.splice(s,1)});export{r,c as s};
//# sourceMappingURL=NPCInteractionTracker-CCwQeo0L.js.map
