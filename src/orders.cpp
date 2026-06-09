#include "pch.h"

OrderBook orderbook;

void Order::PartialUpdate(Quantity quantityfilled){
    //condition ? expression_if_true : expression_if_false;
    qty = (quantityfilled >= qty) ? 0 : (qty - quantityfilled);
    if (qty == 0){
        orderbook.cancel(id);
    }
}

//if quantity = 0, delete the order

void Order::modifyOrder(Price price, Quantity qty, Side side){

    return;
}


//streamlit